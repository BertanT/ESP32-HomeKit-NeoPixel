/*
* Project ESP32-HomeKit-NeoPixel
* Description: Apple HomeKit connected NeoPixel light powered by ESP32 and the HomeSpan Arduino Library!
* Author: M. Bertan Tarakçıoğlu
* Date: May 8th 2022
*/

// Including libraries
#include "HomeSpan.h"
#include <Adafruit_NeoPixel.h>

// Customize the constants below constants to match your setup and liking! :)

// Define HomeKit Constants
#define BRIDGE_NAME "DIY Bridge"
#define LIGHT_NAME "NeoPixel Rainbow Light"

// Define NeoPixel Constants
#define PIXEL_COUNT 32
#define PIXEL_PIN    23
#define PIXEL_TYPE   NEO_GRB+NEO_KHZ800
#define PIXEL_BRIGHTNESS 255 // Between 0 and 255

// Thanks to Adafruit for the NeoPatterns async NeoPixel animations!
// Pattern types supported:
enum pattern
{
    NONE, FADE
};

// NeoPattern Class - derived from the Adafruit_NeoPixel class
class NeoPatterns : public Adafruit_NeoPixel
{
public:
    // Member Variables:
    pattern ActivePattern; // which pattern is running

    unsigned long Interval;   // milliseconds between updates
    unsigned long lastUpdate; // last update of position

    uint32_t Color1, Color2; // What colors are in use
    uint16_t TotalSteps;   // total number of steps in the pattern
    uint16_t Index;      // current step within the pattern

    void (*OnComplete)(); // Callback on completion of pattern

    // Constructor - calls base-class constructor to initialize strip
    NeoPatterns(uint16_t pixels, uint8_t pin, uint8_t type, void (*callback)())
        : Adafruit_NeoPixel(pixels, pin, type)
    {
        OnComplete = callback;
    }

    // Update the pattern
    void Update()
    {
        if ((millis() - lastUpdate) > Interval) // time to update
        {
            lastUpdate = millis();
            switch (ActivePattern)
            {
            case FADE:
                FadeUpdate();
                break;
            default:
                break;
            }
        }
    }

    // Increment the Index and reset at the end
    void Increment()
    {
        Index++;
        if (Index >= TotalSteps)
        {
            Index = 0;
            if (OnComplete != NULL)
            {
                OnComplete(); // call the completion callback
            }
        }
    }

    // Initialize for a Fade
    void Fade(uint32_t color1, uint32_t color2, uint16_t steps, uint8_t interval)
    {
        ActivePattern = FADE;
        Interval = interval;
        TotalSteps = steps;
        Color1 = color1;
        Color2 = color2;
        Index = 0;
    }

    // Update the Fade Pattern
    void FadeUpdate()
    {
        // Calculate linear interpolation between Color1 and Color2
        // Optimize order of operations to minimize truncation error
        uint8_t red = ((Red(Color1) * (TotalSteps - Index)) + (Red(Color2) * Index)) / TotalSteps;
        uint8_t green = ((Green(Color1) * (TotalSteps - Index)) + (Green(Color2) * Index)) / TotalSteps;
        uint8_t blue = ((Blue(Color1) * (TotalSteps - Index)) + (Blue(Color2) * Index)) / TotalSteps;

        ColorSet(Color(red, green, blue));
        show();
        Increment();
    }

    // Calculate 50% dimmed version of a color (used by ScannerUpdate)
    uint32_t DimColor(uint32_t color)
    {
        // Shift R, G and B components one bit to the right
        uint32_t dimColor = Color(Red(color) >> 1, Green(color) >> 1, Blue(color) >> 1);
        return dimColor;
    }

    // Set all pixels to a color (synchronously)
    void ColorSet(uint32_t color)
    {
        for (int i = 0; i < numPixels(); i++)
        {
            setPixelColor(i, color);
        }
        show();
    }

    // Returns the Red component of a 32-bit color
    uint8_t Red(uint32_t color)
    {
        return (color >> 16) & 0xFF;
    }

    // Returns the Green component of a 32-bit color
    uint8_t Green(uint32_t color)
    {
        return (color >> 8) & 0xFF;
    }

    // Returns the Blue component of a 32-bit color
    uint8_t Blue(uint32_t color)
    {
        return color & 0xFF;
    }
};

// Function prototype
void NPFadeComplete();

// Variable to indite wether the NeoPixel was turned off from HomeKit
bool lightWillPowerDown = false;

// Create a NeoPatterns object for our NeoPixel light
NeoPatterns neoPixels(PIXEL_COUNT, PIXEL_PIN, PIXEL_TYPE, &NPFadeComplete);

// Function run when the NeoPatterns animation completes
void NPFadeComplete() {
    // Stop the animation to prevent repetition
    neoPixels.ActivePattern = NONE;

    // Fully turn off the NeoPixels if the user turned it off from HomeKit
    // This is required because the NeoPixels remain lit at the minimum brightness after fading to black
    if (lightWillPowerDown)
    {
        neoPixels.clear();
        neoPixels.show();
        lightWillPowerDown = false;
    }
}

// HomeSpan Apple HomeKit LightBulb object
struct NeoPixelLight : Service::LightBulb
{
    // Defining HomeKit characteristics
    Characteristic::On power{ 0,true };
    Characteristic::Hue H{ 0,true };
    Characteristic::Saturation S{ 0,true };
    Characteristic::Brightness V{ 100,true };

    // Constructor
    NeoPixelLight() : Service::LightBulb()
    {
        // Set the brightness range to 5% to 100%, and step by 1%
        V.setRange(5, 100, 1);
        // Begin NeoPatterns, hence the NeoPixels
        neoPixels.begin();
        // Set the lights according to the last HomeKit values
        update();
    }

    boolean update() override
    {
        // Wether the light is set to on or off from HomeKit
        bool isPowered = power.getNewVal();

        // HSV color values from HomeKit
        float h = map(H.getNewVal<float>(), 0, 360, 0, 65535); // From 0 to 65535
        float s = map(S.getNewVal<float>(), 0, 100, 0, 255);   // From 0 to 255
        float v = map(V.getNewVal<float>(), 0, 100, 0, 255);   // From 0 to 255

        // Get the current NeoPixel color for the fade animation
        uint32_t currentColor = neoPixels.getPixelColor(0);
        // Convert the HSV values from HomeKit to Adafruit NeoPixel ColorHSV object
        uint32_t newColor = neoPixels.ColorHSV(h, s, v);

        // Fade to the new color if light is set to on, fully turn off the NeoPixel if otherwise
        // This is required since the HVS values are not changed if the light is set to off from HomeKit
        if (isPowered)
        {
            neoPixels.Fade(currentColor, newColor, 100, 3);
        }
        else
        {
            uint32_t black = neoPixels.Color(0, 0, 0);
            neoPixels.Fade(currentColor, black, 100, 1);
            lightWillPowerDown = true;
        }

        // Update the NeoPixels
        neoPixels.show();
        return(true);
    }
};


void setup()
{
    // Initialize the serial port for HomeSpan debugging and CLI
    Serial.begin(115200);

    // Initialize HomeSpan HAP Server and bridge
    homeSpan.begin(Category::Lighting, BRIDGE_NAME);
    SPAN_ACCESSORY();

    // Create the NeoPixel light accessory 
    SPAN_ACCESSORY(LIGHT_NAME);
    new NeoPixelLight();
}

void loop()
{
    // Update HomeSpan and continue async NeoPixel animation
    homeSpan.poll();
    neoPixels.Update();
}