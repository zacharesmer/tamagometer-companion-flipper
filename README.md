# Tamagometer Companion Flipper App
The web app it's a companion for: https://zacharesmer.github.io/tamagometer/

Main tamagometer repo: https://github.com/zacharesmer/tamagometer

## Download a pre-built version
Find the most recent workflow run [here](https://github.com/zacharesmer/tamagometer-companion-flipper/actions), download the artifact, and put the `.fap` file on your Flipper's SD card. It automatically builds once a day (or it's supposed to, anyway) to keep up with the most recent release version of the Flipper's firmware. 

## Building and Running
You can also build the app yourself. Install uFBT with the instructions in the [uFBT repo](https://github.com/flipperdevices/flipperzero-ufbt). Essentially:

- Make a Python virtual environment and activate it (optional but recommended if you ever use Python for anything else on your computer)
- `python3 -m pip install --upgrade ufbt`
- From the root of this repository, and with your Flipper plugged into your computer, `ufbt launch`

## Usage
The app needs to be open on your Flipper for the website to work correctly. When the app is opened, it adds the CLI command `tamagometer` to the flipper. When it's closed it removes the command.

## Disclaimer
This project is entirely unofficial and not affiliated with Tamagotchi or Bandai. 

## Other
I learned a lot about Flipper apps and the Flipper in general making this, and I've written some of it down in case it helps anyone else.

[part 1](https://resmer.co.za/ch/posts/flipper-app-general-advice/)

[part 2](https://resmer.co.za/ch/posts/flipper-app-tamagometer/)
