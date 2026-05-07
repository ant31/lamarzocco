[ ] Long press display timer and it starts quick pooling to display timer and weight (if available) as soon as the machine start brewing.
    It must get the brewing time as accurate as possible. Add manuall start /stop button too
    The timer must show the number in big to the decimal
[ ] In the connect qr code page, add a 'connect' that will try to connect to the existing machine (the same thing that it does at the startup)
[ ] Combine the pages pre-brewing, the top area display the in, and bottom area the out. Add a way to navigate between both to edit them . Not sure what's best way why with the encoder it switch with top/bottom, and press activate the edit, then encoder to change value, and press again to confirm. 
[ ] The home page should be a sort of dashboard. It must shows the temp, pre-brew timing,a clock.  can be configured in webpage setting. Do a 24h digital clock. thing of like a smart watch. it's a big 480/480 screen so there's space. Pre-brew in/out can be small and combined.   so maybe a 3 part dashboard for now
like clock half left, brewing temp top right, pre-brew bottom right. 
[ ] Add a counter of number of coffee brewed in the day. A coffee brewed is brewing for more than 10s. 
[ ] when sliding before the QR code, it show a settings page, where you can change the color them (let user rotate theme) a them is combinaison of background/forground colors
    can change the luminosity of the device. Add a reset default button. 
#define SCREEN_BACKLIGHT_PIN 6
const int pwmFreq = 5000;
const int pwmChannel = 0;
const int pwmResolution = 8;

void setupBacklight() {
  ledcSetup(pwmChannel, pwmFreq, pwmResolution);
  ledcAttachPin(SCREEN_BACKLIGHT_PIN, pwmChannel);
  ledcWrite(pwmChannel, 255); // 
}


if (str_uart == "0") { ledcWrite(pwmChannel, 0); }      
else if (str_uart == "1") { ledcWrite(pwmChannel, 64); } 
else if (str_uart == "2") { ledcWrite(pwmChannel, 128);} 
else if (str_uart == "3") { ledcWrite(pwmChannel, 192);} 
else if (str_uart == "4") { ledcWrite(pwmChannel, 255);} 

