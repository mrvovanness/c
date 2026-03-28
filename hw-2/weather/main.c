#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include "cJSON.h"

#define URL_MAX_LEN 256
#define BASE_URL "https://wttr.in/"
#define URL_SUFFIX "?format=j1"

struct response {
	char *data;
	size_t size;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb,
	void *userp) {
	size_t total = size * nmemb;
	struct response *resp = (struct response *)userp;

	char *ptr = realloc (resp->data, resp->size + total + 1);
	if (!ptr) {
		fprintf (stderr, "Error: out of memory\n");
		return 0;
	}

	resp->data = ptr;
	memcpy (resp->data + resp->size, contents, total);
	resp->size += total;
	resp->data[resp->size] = '\0';

	return total;
}

static char *fetch_weather(const char *city) {
	CURL *curl = NULL;
	CURLcode res;
	struct response resp = { 0 };
	char *result = NULL;

	curl = curl_easy_init ();
	if (!curl) {
		fprintf (stderr, "Error: failed to initialize libcurl\n");
		goto cleanup;
	}

	char *escaped_city = curl_easy_escape (curl, city, 0);
	if (!escaped_city) {
		fprintf (stderr, "Error: failed to encode city name\n");
		goto cleanup;
	}

	char url[URL_MAX_LEN];
	int written = snprintf (url, sizeof (url), "%s%s%s", BASE_URL, escaped_city,
		URL_SUFFIX);
	curl_free (escaped_city);
	if (written < 0 || (size_t)written >= sizeof (url)) {
		fprintf (stderr, "Error: city name too long\n");
		goto cleanup;
	}

	curl_easy_setopt (curl, CURLOPT_URL, url);
	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, &resp);
	curl_easy_setopt (curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt (curl, CURLOPT_USERAGENT, "curl/weather-app");

	res = curl_easy_perform (curl);
	if (res != CURLE_OK) {
		fprintf (stderr, "Error: network request failed: %s\n",
			curl_easy_strerror (res));
		goto cleanup;
	}

	long http_code = 0;
	curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);

	if (http_code != 200) {
		fprintf (stderr, "Error: server returned HTTP %ld\n", http_code);
		goto cleanup;
	}

	result = resp.data;
	resp.data = NULL;

cleanup:
	free (resp.data);
	if (curl)
		curl_easy_cleanup (curl);
	return result;
}

static const char *json_string(const cJSON *obj, const char *key) {
	const cJSON *item = cJSON_GetObjectItemCaseSensitive (obj, key);
	if (!cJSON_IsString (item) || item->valuestring == NULL) {
		return NULL;
	}
	return item->valuestring;
}

static const char *json_array_first_value(const cJSON *obj, const char *key) {
	const cJSON *arr = cJSON_GetObjectItemCaseSensitive (obj, key);
	if (!cJSON_IsArray (arr) || cJSON_GetArraySize (arr) == 0) {
		return NULL;
	}
	const cJSON *first = cJSON_GetArrayItem (arr, 0);
	return json_string (first, "value");
}

static int print_weather(const char *json_str, const char *city) {
	cJSON *root = cJSON_Parse (json_str);
	if (!root) {
		fprintf (stderr, "Error: failed to parse JSON response\n");
		return 1;
	}

	/* Check for error responses (wttr.in returns plain text on bad city) */
	const cJSON *current_arr = cJSON_GetObjectItemCaseSensitive (
		root, "current_condition");
	const cJSON *weather_arr = cJSON_GetObjectItemCaseSensitive (
		root, "weather");

	if (!cJSON_IsArray (current_arr) || cJSON_GetArraySize (current_arr) == 0 ||
		!cJSON_IsArray (weather_arr) || cJSON_GetArraySize (weather_arr) == 0) {
		fprintf (stderr, "Error: unknown location \"%s\"\n", city);
		cJSON_Delete (root);
		return 1;
	}

	const cJSON *current = cJSON_GetArrayItem (current_arr, 0);
	const cJSON *today = cJSON_GetArrayItem (weather_arr, 0);

	/* Current weather description */
	const char *desc = json_array_first_value (current, "weatherDesc");

	/* Wind */
	const char *wind_dir = json_string (current, "winddir16Point");
	const char *wind_speed = json_string (current, "windspeedKmph");

	/* Temperature range for today */
	const char *min_temp = json_string (today, "mintempC");
	const char *max_temp = json_string (today, "maxtempC");

	/* Current temperature */
	const char *temp_c = json_string (current, "temp_C");

	if (!desc || !wind_dir || !wind_speed || !min_temp || !max_temp ||
		!temp_c) {
		fprintf (stderr, "Error: incomplete weather data in response\n");
		cJSON_Delete (root);
		return 1;
	}

	printf ("Weather for %s:\n", city);
	printf ("  Conditions:  %s\n", desc);
	printf ("  Temperature: %s°C (range: %s°C .. %s°C)\n",
		temp_c, min_temp, max_temp);
	printf ("  Wind:        %s, %s km/h\n", wind_dir, wind_speed);

	cJSON_Delete (root);
	return 0;
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf (stderr, "Usage: %s <city>\n", argv[0]);
		exit (EXIT_FAILURE);
	}

	const char *city = argv[1];

	char *json_str = fetch_weather (city);
	if (!json_str) {
		exit (EXIT_FAILURE);
	}

	int result = print_weather (json_str, city);
	free (json_str);

	exit (result == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
