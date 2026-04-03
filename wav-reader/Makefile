CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -fPIC -DPACKAGE=\"mywavsrc\" \
        $(shell pkg-config --cflags gstreamer-1.0 gstreamer-base-1.0)
LDFLAGS = -shared $(shell pkg-config --libs gstreamer-1.0 gstreamer-base-1.0)

libgstmywavsrc.dylib: gstmywavsrc.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

play: libgstmywavsrc.dylib
	GST_PLUGIN_PATH=. gst-launch-1.0 mywavsrc location=test.wav ! audio/x-raw,format=S16LE,channels=1,rate=48000 ! autoaudiosink

clean:
	rm -f libgstmywavsrc.dylib
