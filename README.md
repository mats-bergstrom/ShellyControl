# ShellyControl
Control a Shelly Plus Plug S using MQTT.

## Background
I have solar panels that sometimes give more energy than my house nomally uses and the amount of excess power is accessible as an MQTT topic sent every 10 seconds where negative values indicate excess (negative use) and positive values is power being taken from the grid.
This gives me a way to use excess energy instead of exporting it.

## Function
This program controls a Shelly Plus Plug S using MQTT topics so that if the excess power is larger than a given value (for a given number of incoming power messages), the Shelly is turned on, and if the excess power is less than a given value (for a given number of incoming power messages) the Shelly is turned off.
If there are no incoming power messages for a duration given by the ```timeout``` value, the Shelly is turned off.

The behaviour is controlled by the config file
```
# Config for shellyctrl

#	Broker addr	port	mqtt-id
mqtt	127.0.0.1	1883	shellyctrl


# Ptopic
ptopic	power/topic

# Timeout of incoming power topic values
timeout	120

# PowerOn	Count (Turn ON after Count times above PowerOn value)
POn	750	20

# Power off	Count (Turn OFF after Count times below PowerOff value)
POff	50	6

# ShellyTopic to send on/off commands to
stopic	 shellies/shellyplug-s-012345/relay/0/command
```
