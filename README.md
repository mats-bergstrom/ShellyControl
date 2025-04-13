# ShellyControl
Control a Shelly Plus Plug S using MQTT.

## Background
I have solar panels that sometimes give more energy than my house nomally uses and the amount of excess power is accessible as an MQTT topic sent every 10 seconds where negative values indicate excess (negative use) and positive values is power being taken from the grid.
This gives me a way to use excess energy instead of exporting it.

## Function
This program controls a Shelly Plus Plug S using MQTT topics.

The MODE topic controls how the program operates:
* ON, the program keeps the Shelly ON.
* OFF, the program keeps the Shelly OFF.
* POWER, the Shelly is turned on if the POWER topic is larger than a given value (for a given number of incoming power messages), and the Shelly is turned off if the POWER topic is less than a given value (for a given number of incoming power messages).
If there are no incoming power messages for a duration given by the ```timeout``` value, the Shelly is turned off.

The behaviour is controlled by the config file
```
# Config for shellyctrl

#	Broker addr	port	mqtt-id
mqtt	127.0.0.1	1883	shellyctrl

# Ptopic
power-topic	power/topic

# Timeout of incoming power topic values
timeout	120

# Mode topic, 0/1/10 = ON/OFF/POWER
mode-topic	shelly/012345/mode

# To set the mode topic:
# mosquitto_pub  -h pih -t shelly/012345/mode -m 0 -r
# mosquitto_pub  -h pih -t shelly/012345/mode -m 1 -r
# mosquitto_pub  -h pih -t shelly/012345/mode -m 10 -r

# PowerOn	Count (Turn ON after Count times above PowerOn value)
POn	750	20

# Power off	Count (Turn OFF after Count times below PowerOff value)
POff	50	6

# ShellyTopics
# cmd-topic	Topic to send "on"/"off" commands to the Shelly
cmd-topic	 shellies/shellyplug-s-012345/relay/0/command

# state-topic	Shelly state input topic, "on" or "off".
state-topic	 shellies/shellyplug-s-012345/relay/0
```


## Installation

Build:
```
make
```

Install:
``
make install
```

Use systemctrl to enable and run:
```
sudo systemctl enable shellyctrl
sudo systemctl start shellyctrl
sudo systemctl status shellyctrl
```
