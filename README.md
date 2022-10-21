# FlexTrak AVR

This is the firmware for the FlexTrak board:

https://store.uputronics.com/index.php?route=product/product&product_id=118

See separate FlexTrak repository for the Pi host software in Python:

https://github.com/daveake/flextrak

For documentation on this firmware. the Pi host software, and the serial protocol that they share, see the FlexTrak manual:

https://github.com/daveake/HAB-Documentation/blob/main/FlexTrak%20Manual.pdf



## Releases

V1.22	-	Added option to send the field list

V1.21	-	Bug fix - saved settings not being loaded from flash.

V1.20	-	Reduced RAM footprint.  Reset radio on startup.  Default 434.225MHz Mode 1.

V1.12	-	6 custom host-populated telemetry fields, cutdown, LoRa uplink

V1.01	-	Host priority mode, ability to fake GPS from host (for replaying flights)