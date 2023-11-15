#include <Arduino.h>
#include <Bounce2.h>
#include <Encoder.h>
#include <array>

constexpr int encoder_pins[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
constexpr int button_pins[] = {10, 11, 12, 14, 15};

constexpr uint16_t num_encoder_pins =
    sizeof(encoder_pins) / sizeof(encoder_pins[0]);
static_assert(num_encoder_pins % 2 == 0, "Number of pins must be even");
constexpr uint16_t num_encoders = num_encoder_pins / 2;
constexpr uint16_t num_buttons = sizeof(button_pins) / sizeof(button_pins[0]);

constexpr bool contains_led_builtin(const int pins[], uint16_t num_pins) {
    for (uint16_t i = 0; i < num_pins; i++) {
        if (pins[i] == LED_BUILTIN) {
            return true;
        }
    }
    return false;
}
static_assert(!contains_led_builtin(encoder_pins, num_encoder_pins),
              "Don't use the built-in LED pin");
static_assert(!contains_led_builtin(button_pins, num_buttons),
              "Don't use the built-in LED pin");

class EncoderMeta {
   public:
    Encoder *encoder = nullptr;
    int32_t previous_position = 0;
    uint32_t previous_millis = 0;
    uint8_t control = 0;
};

class ButtonMeta {
   public:
    Bounce2::Button *button = new Bounce2::Button();
    uint8_t note = 0;
};

EncoderMeta encoders[num_encoders];
ButtonMeta buttons[num_buttons];

void setup() {
    // Loop through pairs of pins
    for (uint16_t i = 0; i < num_encoders; i++) {
        uint16_t pin_a = encoder_pins[i * 2];
        uint16_t pin_b = encoder_pins[i * 2 + 1];
        // Encoder library does the pullups for us.
        encoders[i].encoder = new Encoder(pin_a, pin_b);
        // Just mirroring what the X-Touch Mini does.
        encoders[i].control = 0x10 + i;
    }
    for (uint16_t i = 0; i < num_buttons; i++) {
        buttons[i].button->attach(button_pins[i], INPUT_PULLUP);
        buttons[i].button->interval(5);
        buttons[i].note = 0x20 + i;
    }
    // Wait for the pullups to complete.
    // https://www.pjrc.com/teensy/td_digital.html says that this is plenty
    delayMicroseconds(10);
}

void check_encoder(EncoderMeta *encoder_meta) {
    int32_t position = encoder_meta->encoder->read();
    int32_t rotation = position - encoder_meta->previous_position;
    encoder_meta->previous_position = position;

    if (rotation != 0) {
        uint32_t now = millis();
        uint32_t elapsed = millis() - encoder_meta->previous_millis;
        encoder_meta->previous_millis = now;
        if (elapsed < 100) {
            rotation *= (100 / elapsed);
        }
        // Encoders can be a bit chattery, so debounce. 2ms seems to work well.
        if (elapsed > 2) {
            // Max value is 127, and we want to use 0-63 for one direction and
            // 64-127 for the other, so clamp to -63 to 63.
            // Yes, 0 and 64 go unused, but that's fine. This feels neat, and
            // mirrors what the X-Touch Mini does.
            if (rotation > 63) {
                rotation = 63;
            } else if (rotation < -63) {
                rotation = -63;
            }
            if (rotation < 0) {
                // this is 64 + (-rotation)
                rotation = (int32_t)64 - rotation;
            }
            // usbMIDI.sendControlChange(encoder_meta->control, rotation, 1);
        }
    }
}

void check_button(ButtonMeta *button_meta) {
    button_meta->button->update();
    if (button_meta->button->pressed()) {
        usbMIDI.sendNoteOn(button_meta->note, 127, 1);
    } else if (button_meta->button->released()) {
        usbMIDI.sendNoteOff(button_meta->note, 0, 1);
    }
}

void loop() {
    for (uint16_t i = 0; i < num_encoders; i++) {
        check_encoder(&encoders[i]);
    }
    for (uint16_t i = 0; i < num_buttons; i++) {
        check_button(&buttons[i]);
    }
    // Consume all incoming MIDI messages to prevent hangups.
    while (usbMIDI.read()) {
    }
}