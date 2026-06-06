#pragma once

#include "ofxMidi.h"

/**
 * LED control for the Novation Launch Control XL 3.
 *
 * The XL3 requires a separate DAW MIDI port and DAW mode enabled
 * to control RGB LEDs via SysEx messages.
 *
 * Reference: AGENTS-how-to-control-launchcontrolxl3-leds.md
 */
class ofxLaunchControlXL3Leds {
public:
  // Predefined RGB colors (values 0-127)
  struct Color {
    int r, g, b;

    static Color off() { return {0, 0, 0}; }
    static Color red() { return {127, 0, 0}; }
    static Color green() { return {0, 127, 0}; }
    static Color blue() { return {0, 0, 127}; }
    static Color yellow() { return {127, 127, 0}; }
    static Color cyan() { return {0, 127, 127}; }
    static Color magenta() { return {127, 0, 127}; }
    static Color white() { return {127, 127, 127}; }
    static Color orange() { return {127, 64, 0}; }
    static Color purple() { return {64, 0, 127}; }

    // Softer alternatives (roughly 50% brightness)
    static Color softRed() { return {64, 0, 0}; }
    static Color softGreen() { return {0, 64, 0}; }
    static Color softBlue() { return {0, 0, 64}; }
    static Color softYellow() { return {64, 64, 0}; }
    static Color softCyan() { return {0, 64, 64}; }
    static Color softMagenta() { return {64, 0, 64}; }
    static Color softWhite() { return {64, 64, 64}; }
    static Color softOrange() { return {64, 32, 0}; }
  };

  // Control indices for LED addressing
  struct Index {
    // Faders (indices 5-12)
    static constexpr int faderFirst = 5;
    static constexpr int faderLast = 12;

    // Encoders Row 1 - Top (indices 13-20)
    static constexpr int encoderRow1First = 13;
    static constexpr int encoderRow1Last = 20;

    // Encoders Row 2 - Middle (indices 21-28)
    static constexpr int encoderRow2First = 21;
    static constexpr int encoderRow2Last = 28;

    // Encoders Row 3 - Bottom (indices 29-36)
    static constexpr int encoderRow3First = 29;
    static constexpr int encoderRow3Last = 36;

    // Track Focus Buttons - Top Row (indices 37-44)
    static constexpr int buttonTopRowFirst = 37;
    static constexpr int buttonTopRowLast = 44;

    // Track Control Buttons - Bottom Row (indices 45-52)
    static constexpr int buttonBottomRowFirst = 45;
    static constexpr int buttonBottomRowLast = 52;
  };

  ofxLaunchControlXL3Leds();
  ~ofxLaunchControlXL3Leds();

  /**
   * Setup the LED controller by finding and opening the DAW MIDI port.
   * @param enableDaw If true, enables DAW mode (required for LED control but
   *                  overrides Custom Mode mappings). If false, keeps Custom Mode
   *                  active but LED commands may not work.
   * @return true if DAW port found and opened successfully
   */
  bool setup(bool enableDaw = true);

  /**
   * Shutdown the LED controller, disabling DAW mode and closing the port.
   */
  void shutdown();

  /**
   * Check if the LED controller is connected and ready.
   */
  bool isConnected() const { return connected; }

  /**
   * Set an LED to a specific RGB color.
   * @param controlIndex The control index (5-52, see Index struct)
   * @param r Red intensity (0-127)
   * @param g Green intensity (0-127)
   * @param b Blue intensity (0-127)
   */
  void setLED(int controlIndex, int r, int g, int b);

  /**
   * Set an LED to a predefined color.
   */
  void setLED(int controlIndex, const Color& color);

  /**
   * Set a button LED (buttonNum 1-16).
   * Buttons 1-8 are the top row, 9-16 are the bottom row.
   */
  void setButtonLED(int buttonNum, int r, int g, int b);
  void setButtonLED(int buttonNum, const Color& color);

  /**
   * Set an encoder LED (encoderNum 1-24).
   * Encoders 1-8 are row 1 (top), 9-16 are row 2, 17-24 are row 3.
   */
  void setEncoderLED(int encoderNum, int r, int g, int b);
  void setEncoderLED(int encoderNum, const Color& color);

  /**
   * Set a fader LED (faderNum 1-8).
   */
  void setFaderLED(int faderNum, int r, int g, int b);
  void setFaderLED(int faderNum, const Color& color);

  /**
   * Set all LEDs in the top button row (buttons 1-8) to the same color.
   */
  void setTopButtonRowLEDs(int r, int g, int b);
  void setTopButtonRowLEDs(const Color& color);

  /**
   * Set all LEDs in the bottom button row (buttons 9-16) to the same color.
   */
  void setBottomButtonRowLEDs(int r, int g, int b);
  void setBottomButtonRowLEDs(const Color& color);

  /**
   * Turn off all LEDs.
   */
  void clearAllLEDs();

  /**
   * Get a pointer to the MIDI output for sharing with other controllers (e.g., display).
   * Returns nullptr if not connected.
   */
  ofxMidiOut* getMidiOut() { return connected ? &midiOut : nullptr; }

private:
  void enableDawMode();
  void disableDawMode();

  ofxMidiOut midiOut;
  bool connected;
  bool dawModeEnabled;
};
