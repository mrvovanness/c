# mywavsrc — GStreamer-плагин для воспроизведения WAV

GStreamer source-элемент для чтения 16-битных little-endian несжатых моно WAV-файлов (PCM).

## Зависимости

- GLib / GStreamer 1.x
- pkg-config

```
brew install pkg-config gstreamer
```

## Сборка

```
make
```

Компилируется с `-Wall -Wextra -Wpedantic -std=c11` без предупреждений.

## Запуск

```
make play
```

Или вручную:

```
GST_PLUGIN_PATH=. gst-launch-1.0 mywavsrc location=test.wav ! audio/x-raw,format=S16LE,channels=1,rate=48000 ! autoaudiosink
```

## Дополнительные команды

```
make inspect   # информация о плагине
make clean     # удалить собранную библиотеку
```
