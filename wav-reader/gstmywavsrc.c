#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include <stdio.h>
#include <string.h>

/* --- Type declaration (no separate header needed) --- */

#define GST_TYPE_MY_WAV_SRC (gst_my_wav_src_get_type())
G_DECLARE_FINAL_TYPE(GstMyWavSrc, gst_my_wav_src, GST, MY_WAV_SRC, GstBaseSrc)

struct _GstMyWavSrc {
    GstBaseSrc parent;
    gchar     *location;
    FILE      *fp;
    guint32    sample_rate;
    guint32    data_offset;
    guint32    data_size;
};

enum { PROP_0, PROP_LOCATION };

/* --- Pad template: what this element can output --- */

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("audio/x-raw, format=S16LE, channels=1, "
                     "rate=[1,2147483647], layout=interleaved")
);

G_DEFINE_TYPE(GstMyWavSrc, gst_my_wav_src, GST_TYPE_BASE_SRC)

/* --- Helpers --- */

static guint16 read_u16(const guint8 *p)
{
    return (guint16)(p[0] | (p[1] << 8));
}

static guint32 read_u32(const guint8 *p)
{
    return (guint32)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

/* --- WAV header parsing --- */

static gboolean parse_wav_header(GstMyWavSrc *src)
{
    guint8 buf[12];
    if (fread(buf, 1, 12, src->fp) != 12)
        return FALSE;
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0)
        return FALSE;

    gboolean found_fmt = FALSE;

    for (;;) {
        guint8 hdr[8];
        if (fread(hdr, 1, 8, src->fp) != 8)
            return FALSE;
        guint32 size = read_u32(hdr + 4);

        if (memcmp(hdr, "fmt ", 4) == 0) {
            guint8 fmt[16];
            if (size < 16 || fread(fmt, 1, 16, src->fp) != 16)
                return FALSE;
            if (read_u16(fmt) != 1 || read_u16(fmt + 2) != 1 ||
                read_u16(fmt + 14) != 16)
                return FALSE;
            src->sample_rate = read_u32(fmt + 4);
            if (size > 16)
                fseek(src->fp, (long)(size - 16), SEEK_CUR);
            found_fmt = TRUE;
        } else if (memcmp(hdr, "data", 4) == 0 && found_fmt) {
            src->data_size = size;
            src->data_offset = (guint32)ftell(src->fp);
            return TRUE;
        } else {
            fseek(src->fp, (long)size, SEEK_CUR);
        }
    }
}

/* --- GstBaseSrc virtual methods --- */

static gboolean gst_my_wav_src_start(GstBaseSrc *basesrc)
{
    GstMyWavSrc *src = GST_MY_WAV_SRC(basesrc);

    if (src->location == NULL)
        return FALSE;
    src->fp = fopen(src->location, "rb");
    if (src->fp == NULL)
        return FALSE;
    if (!parse_wav_header(src)) {
        fclose(src->fp);
        src->fp = NULL;
        return FALSE;
    }
    return TRUE;
}

static gboolean gst_my_wav_src_stop(GstBaseSrc *basesrc)
{
    GstMyWavSrc *src = GST_MY_WAV_SRC(basesrc);
    if (src->fp != NULL) {
        fclose(src->fp);
        src->fp = NULL;
    }
    return TRUE;
}

static gboolean gst_my_wav_src_get_size(GstBaseSrc *basesrc, guint64 *size)
{
    *size = GST_MY_WAV_SRC(basesrc)->data_size;
    return TRUE;
}

static gboolean gst_my_wav_src_is_seekable(GstBaseSrc *basesrc)
{
    (void)basesrc;
    return TRUE;
}

static GstFlowReturn gst_my_wav_src_fill(GstBaseSrc *basesrc, guint64 offset,
                                          guint length, GstBuffer *buf)
{
    GstMyWavSrc *src = GST_MY_WAV_SRC(basesrc);

    if (offset >= src->data_size)
        return GST_FLOW_EOS;
    if (offset + length > src->data_size)
        length = (guint)(src->data_size - offset);

    fseek(src->fp, (long)(src->data_offset + offset), SEEK_SET);

    GstMapInfo map;
    gst_buffer_map(buf, &map, GST_MAP_WRITE);
    size_t n = fread(map.data, 1, length, src->fp);
    gst_buffer_unmap(buf, &map);

    if (n == 0)
        return GST_FLOW_EOS;
    gst_buffer_set_size(buf, (gssize)n);
    return GST_FLOW_OK;
}

static GstCaps *gst_my_wav_src_get_caps(GstBaseSrc *basesrc, GstCaps *filter)
{
    GstMyWavSrc *src = GST_MY_WAV_SRC(basesrc);

    if (src->sample_rate == 0)
        return gst_pad_get_pad_template_caps(GST_BASE_SRC_PAD(basesrc));

    GstCaps *caps = gst_caps_new_simple(
        "audio/x-raw",
        "format",   G_TYPE_STRING, "S16LE",
        "channels", G_TYPE_INT,    1,
        "rate",     G_TYPE_INT,    (gint)src->sample_rate,
        "layout",   G_TYPE_STRING, "interleaved",
        NULL);

    if (filter != NULL) {
        GstCaps *tmp = gst_caps_intersect(caps, filter);
        gst_caps_unref(caps);
        caps = tmp;
    }
    return caps;
}

/* --- GObject property boilerplate --- */

static void gst_my_wav_src_set_property(GObject *obj, guint id,
                                        const GValue *val, GParamSpec *ps)
{
    if (id == PROP_LOCATION) {
        GstMyWavSrc *src = GST_MY_WAV_SRC(obj);
        g_free(src->location);
        src->location = g_value_dup_string(val);
    } else {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, ps);
    }
}

static void gst_my_wav_src_get_property(GObject *obj, guint id,
                                        GValue *val, GParamSpec *ps)
{
    if (id == PROP_LOCATION)
        g_value_set_string(val, GST_MY_WAV_SRC(obj)->location);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, ps);
}

static void gst_my_wav_src_finalize(GObject *obj)
{
    GstMyWavSrc *src = GST_MY_WAV_SRC(obj);
    g_free(src->location);
    if (src->fp != NULL)
        fclose(src->fp);
    G_OBJECT_CLASS(gst_my_wav_src_parent_class)->finalize(obj);
}

/* --- Class and instance init --- */

static void gst_my_wav_src_class_init(GstMyWavSrcClass *klass)
{
    GObjectClass    *obj_class = G_OBJECT_CLASS(klass);
    GstElementClass *elem_class = GST_ELEMENT_CLASS(klass);
    GstBaseSrcClass *src_class = GST_BASE_SRC_CLASS(klass);

    obj_class->set_property = gst_my_wav_src_set_property;
    obj_class->get_property = gst_my_wav_src_get_property;
    obj_class->finalize     = gst_my_wav_src_finalize;

    g_object_class_install_property(obj_class, PROP_LOCATION,
        g_param_spec_string("location", "File Location",
            "Path to the WAV file", NULL,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gst_element_class_set_static_metadata(elem_class,
        "My WAV Source", "Source/Audio",
        "Reads 16-bit mono PCM WAV files", "Vlad X");
    gst_element_class_add_static_pad_template(elem_class, &src_template);

    src_class->start       = gst_my_wav_src_start;
    src_class->stop        = gst_my_wav_src_stop;
    src_class->get_size    = gst_my_wav_src_get_size;
    src_class->is_seekable = gst_my_wav_src_is_seekable;
    src_class->fill        = gst_my_wav_src_fill;
    src_class->get_caps    = gst_my_wav_src_get_caps;
}

static void gst_my_wav_src_init(GstMyWavSrc *src)
{
    gst_base_src_set_format(GST_BASE_SRC(src), GST_FORMAT_BYTES);
    (void)src;
}

/* --- Plugin entry point --- */

static gboolean plugin_init(GstPlugin *plugin)
{
    return gst_element_register(plugin, "mywavsrc", GST_RANK_NONE,
                                GST_TYPE_MY_WAV_SRC);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, mywavsrc,
    "16-bit mono WAV source", plugin_init, "1.0", "LGPL", "mywavsrc",
    "https://example.com")
