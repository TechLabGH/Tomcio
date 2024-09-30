# Tomcio
**Film Development Timer/Controller/Helper**

The project has been built to help me in developing 35mm films. This process requires strictly following timeline depending on used chemicals and film recomendations.

It supports four stages (this is set for Ilford Delta 100 / Ilfosol-3 / Ilfostop Stop Bath / Rapid Fixer)

* **Development**

  Initial agitation during first 10 sec - invert tank 4 times. Next repeat this on beginning of every next minute. Total development time is 7:30

  Drain off developer 10 sec before end

* **Stop bath**

  I use 2 min stop bath with 4 tank inversions at the beginning and after 1 min

* **Fix**

  This is 4 min process - agitation is the same as in development.

* **Rinse**

  This is three step process - fill with water, invert tank 5 timex / replace water, invert 10 times / replace water, invert 20 times

Project use NEMA motor to invert tank. I built 3D printed mount where tank may be quickly and securely attached. I used 6 x 200mm 2020 profiles to build frame. Additionally there is vibration motor attached - it's running at the end of agitation to remove any bubles from film surface.

Cortoller was built around ESP32-S3 module. Program is developed in PlatformIO Arduino framework. Code looks horrible :) but I'm beginner, cleanning and improoving it would be challenge itself for me. I used 4" TFT touchscreen with ST7796S IC. 

