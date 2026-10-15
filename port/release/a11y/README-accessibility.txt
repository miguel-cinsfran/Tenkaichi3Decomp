Dragon Ball Z Budokai Tenkaichi 3 with a screen reader

To install the game:
1. Put this zip and your disc image of the game (the file that ends in .iso) in the same folder.
2. Extract the zip: right-click it, "Extract All", then "Extract".
3. Go into the Tenkaichi3Decomp folder and open Tenkaichi3Decomp-setup.

The setup finds the image by itself, installs the game in a minute or two, tells you how it goes through your screen reader and opens the game at the end. If it says it found no image, copy the .iso file into the Tenkaichi3Decomp folder with the setup still open: it finds it by itself once the copy has ended.

To play later, open Tenkaichi3Decomp. Opening the game before it is installed opens the setup.

If Windows says "Windows protected your PC", choose "More info" and then "Run anyway". It says so because the program has no digital signature.

You need your own disc image of the USA release (SLUS-21678), with the game's programs unmodified. Do not share or ask for disc images.

In the game:
- Press the left stick in, like a button (L3 on a DualSense, LS on an Xbox controller). In a fight it gives your health; twice quickly, the rival's. Outside a fight it says the last message again.
- At first the health is two beeps, louder the less health is left. For the screen reader to say health, ki and blast: settings window, Accessibility tab, "Gauges on request", and choose "Screen reader" or "Sound and screen reader".
- On the keyboard: F2 says the last message again. F3 and F4 give player 1's and player 2's gauges.
- The settings window opens with F1, or with the controller's two middle buttons together (Create and Options on a DualSense, View and Menu on an Xbox controller). The right stick changes the focused value.
- Its Accessibility tab has the fight's alerts: distance to the rival, edge of the stage, health, transformation and Ultimate Blast, and gauges on request, with their volumes. Distance, edge and health come switched off: switch them on there.
- In the pause menu, the skill list is read one move at a time, with the buttons of your controller.

The screen reader works with NVDA and JAWS; without them it uses Windows' own voice. It speaks through Prism (MPL-2.0; its source is at https://github.com/ethindp/prism; its licences are in licenses/prism). The language is Windows' language; for another one, write a11y_lang="en" (or the code you want) in bt3_settings.txt.
