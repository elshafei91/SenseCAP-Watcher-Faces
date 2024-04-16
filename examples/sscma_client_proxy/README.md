**SSCMA Client Proxy Example**

### Overview

This code serves as a proxy program for SSCMA via MQTT.

### Configuration

To configure the program, you need to enter `menuconfig` and set up the
following parameters:

- WiFi SSID and password
- MQTT broker address
- Credentials Here, I will provide images for the configuration. Use the command
  `idf.py menuconfig` to enter `menuconfig`.


    ![menuconfig](./assert/menuconfig.png)

### Installation

Make sure to install `python-sscma` using the following command:

```
pip install python-sscma
```

### Sampling and Visualization

To perform sampling and visualization using `python-sscma`, use the following
command:

```
sscma.cli client --broker <BROKER_ADDR> --username <USERNAME> --password <PASSWORD> --device <CLIENT-ID>
```

The format for the Client ID is as follows:

```
watcher-%02x%02x%02x%02x%02x%02x
```

This format incorporates the MAC address.

Feel free to reach out if you have any questions or need further assistance!
