# SimpleDRO
DRO-like readout of digital caliper or digital dial indicator using Raspberry Pi Pico and Arduino IDE

If you do not have a digital readout (DRO) on your lathe or millig machine, you can use this little helper to get precise readings with DRO-like functions. 

Features:
- Show readout of caliper on a display
- Switch between linear and diameter mode (useful for turning)
- Invert reading direction
- Set Zero or enter reference value

Hardware used:
- Raspberry Pi Pico
- Waveshare Pico Restouch 3.5" touch display module: https://www.waveshare.com/pico-restouch-lcd-3.5.htm
- Cheap Chinese digital caliper or digital dial indicator with data port. 
- Case

Software
- Arduino IDE (www.arduino.cc)
- Configure Arduino IDE as described in https://www.waveshare.com/wiki/Pico-ResTouch-LCD-3.5
  - install Raspberry Pi Pico Board driver by Earle F. Philhower III (https://github.com/earlephilhower/arduino-pico)
  - install TFT_eSPI library and configure it as described in the Waveshare Wiki Page mentionned above
  
Electronics
Many of the cheap Chinese calipers offer 4 connections for data readout. (Unfortunately, there is no list of calipers that use the protocoll described below nor is there any indication when ordering. Many do use this protocol, but there are also others. You probably have to try first with one that is lying around in your workshop or you just order one and try.)
Measured values are periodically sent (e.g. every 100 ms), even if the caliper is in the OFF-state. Most cheap calipers use a simple protocol with 24 bits (bit 0 to bit 23). The first few bits contain the numeric value in a binary coding, bit 20 contains the sign (+ or -), bit 23 contains the unit (inch or mm). Checking the state of the data signal at each falling edge of the clock signal gives the state of the individual bits. Here is an example of the readings "0.00" and "0.08":

![image](https://user-images.githubusercontent.com/26085758/211794586-c7c69b70-3a2f-4531-b8a5-781749ff11e0.png)


The calipers works with 1.5 V so that the clock and data signal have to be amplified to be used as inputs for the Pico. This is done with two transistors:

![image](https://user-images.githubusercontent.com/26085758/211794749-2ba3e11d-9eaf-4b0d-9d94-d278f309c582.png)

If you connect the "Vpos" pin as shown in the schematics above, you can power the caliper and do not need a battery anymore. 
