# Smart Circulator
A smart diifferental temperature circulator powered by ESP32.

## Features
- Fully automated differential temperature controller
- Wireless communication between all three units
- ESP's LR mode is used, up to 1KM Line of Sight range
- All options configurable from an easy to navigate menu
- Error detection and failsafes to avoid running the pump forever
- Temperature monitoring with the push of a button from two different screens

## The Problem
The client has a relatively big two story house with a solar water heater on the roof. The hot water took way too long to reach the faucet. Buffer tanks exist to mitigate this issue but they are way too big and expensive.

### The Plumber's Solution
The plumber installed an electrical water heater without plugging it to the wall, instead he used a wilo pump/circulator to perform the circulation whch he plugged to a smart plug.

### The Automated Solution
The current solution works but has a lot of issues as it is not automated and there is no way of telling the temperature of both tanks. My solution was to build three units, the first one to be installed on the roof to transmit the temperature of the solar heater's tank, the second to be installed outside the bathroom so the user can monitor the temperatures and the status of the pump and the third on the buffer tank to measure the temperature and control the pump with a relay. All three units communicate with ESP's proprietary Long Range (LR) mode.

## Units
### Parts I Used
- 3x [ESP32 Type C + Antenna Kit](https://www.aliexpress.com/item/1005008929118730.html)
- 3x [5V 2A Power Supplies](https://www.aliexpress.com/item/1005009095158077.html)
- 2x [DS18B20 Fake and 4.7KΩ resistor breakout board](https://www.aliexpress.com/item/1005001601986600.html)
- 1x [JQC-3FF-S-Z 5V Low Level Trigger Relay](https://www.aliexpress.com/item/1005005445591964.html)
- 1x [EC11 360° Rotary Encoder and Button](https://www.aliexpress.com/item/1005006781629675.html) although I'd recommend button boards instead like [this](https://www.aliexpress.com/item/1005006402682474.html) or [this](https://www.aliexpress.com/item/1005004358039716.html) one
- 1x [On/Off Switch](https://www.aliexpress.com/item/1005007535901223.html)
- 1x [Metal Momentary Push Button with 3-6V LED Ring](https://www.aliexpress.com/item/1005006090350341.html)
- 1x [SH1107 128x128 1.5" OLED Screen](https://www.aliexpress.com/item/32899679817.html) for unit #3
- 1x [H1106 128X64 1.3" OLED Screen](https://www.aliexpress.com/item/1005007571793140.html) for unit #2
- [Prototyping Boards](https://www.aliexpress.com/item/1005008177952205.html) are optional but very recommended for better cable management
- Boxes to house everything

### Tips
- On the units that have an OLED screen we use GPIO 21 for SDA and GPIO 22 for SCL (the defaults) but you can use `Wire.begin(SDA, SCL);` to set custom pins
- On unit #2, I had to screw the screen upside down so I used `U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2);` to flip it (180°), you can remove it if you install it the opposite way
- I bought DS18B20 sensors from AliExpress that had a 3.5mm stereo jack ending, in retrospect that was a terrible idea as plugging/unplugging it while the ESP32 is running WILL damage the data pin. You can use an RJ11 or RJ45 as a better alternative that will not short anything
- Currently the button's ring on unit #2 is constantly illuminated but, with a different wiring, it's possible to control it with the ESP32 and make it only light up in specific scenarios (pump is running, screen is on, etc.). AliExpress also has RGB buttons so you could also use different colors to convey different kinds of information (red for error, blue for very cold water, etc.)
- Sockets with wires are also available for the push button
- I couldn't get debouncing to properly work on the EC11 rotary encoder in time so I had to make it only turn clockwise, I recommend opting for buttons instead
- You can use Arduino's IDE to program the ESP32 boards, the board I chose is `ESP32 Dev Module` and `115200` as the upload speed
- You can use the serial console of Arduino IDE with `Serial.begin(115200);` and then set your baud rate to that number too. You may have to unplug and plug again the ESP32 a few times after opening the console to get readable output instead of question mark blocks
- All the libraries I used are available in Arduino's IDE
- Don't forget to get the mac address of your own ESP32 and replace those in the source
- DS18B20 is a bit innacurate and unstable but it is a popular cheap solution, you can calibrate it if you want but it will never be ver accurate or stable, especially the clones sold on AliExpress
- The breakout board for the DS18B20 can be replaced with a normal 4.7KΩ resistor
- You can use different size oleds, I just used the ones that made sense for my usecase
- The On/Off switch on unit #3 is used to reboot the unit in case the runtime exceeds the max runtime set which means something is wrong, if that happens the pump won't run until restarted. It can be also used to turn off the unit completely of course
- You can wire a switch parallel to the relay to manually run the pump easily
- You can substitute the simple parts like switches and buttons with parts that you already have or ones that are available in local stores
- Make sure the box that will be used for unit #1 is waterproof
- Cutting a square hole can be painful, for unt #2 I bought a hard plastic box so I had to drill many holes and then file it, for unit #3 I bought a soft plastic and I was able to cut the hole with an x-acto knife

### Unit 1 (Solar Monitor)
<details>
  <summary>Schematic</summary>
  <img width="713" height="1411" alt="unit1" src="https://github.com/user-attachments/assets/996b81f7-7384-4cd2-899b-7a8234b1efbc" />
</details>


### Unit 2 (Bathroom Dashboard, Optional)
<details>
  <summary>Schematic</summary>
  <img width="1353" height="822" alt="unit2" src="https://github.com/user-attachments/assets/8b444066-6a73-4da9-b64e-2bd8450c6818" />
</details>


### Unit 3 (Main Unit)
<details>
  <summary>Schematic</summary>
  <img width="1836" height="2085" alt="unit3" src="https://github.com/user-attachments/assets/05f12927-ab2e-4769-8d9b-cabdce213950" />
</details>

