# Open RealtimeAPI Embedded SDK

## Build

```sh
git submodule update --init --recursive
idf.py build flash monitor
```

## Usage

Configure wifi and openai key through the serial command line.


![console](console.png "console")

```sh
openai_api -k xxx
wifi_sta  -s  xxx -p xxx
```
