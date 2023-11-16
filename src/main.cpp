#include <Arduino.h>
#include <Bounce2.h>
#define ENCODER_OPTIMIZE_INTERRUPTS
#include <Encoder.h>

// Configure the pins of your encoders here. The first pin should be connected
// to the A pin of the first encoder, the second pin to the B pin of the first
// encoder, the third pin to the A pin of the second encoder, etc. You should
// always have an even number of pins, obviously. The common pin of each encoder
// should be connected to ground.
constexpr int encoder_pins[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
// Configure the pins of your buttons here. These can be regular buttons or the
// switch functionality of your encoders. One pin per button, and the other pin
// of each button should be connected to ground. 
constexpr int button_pins[] = {10, 11, 12, 14, 15};
// Control Channel ID for the first encoder. Other encoders will simply use the
// next control channel ID. The X-Touch Mini uses 0x10 for the first encoder,
// 0x11 for the second, etc. That seems to work well.
const uint8_t first_encoder_control_channel = 0x10;
// Note ID for the first button. Other buttons will simply use the next note ID.
// The X-Touch Mini uses 0x20 for the first button, 0x21 for the second, etc.
const uint8_t first_button_note = 0x20;

/////////////////////////////////////////////////////////////
// You should not need to change anything below this line. //
/////////////////////////////////////////////////////////////

// Some sanity checks.
constexpr uint8_t num_encoder_pins =
    sizeof(encoder_pins) / sizeof(encoder_pins[0]);
static_assert(num_encoder_pins % 2 == 0, "Number of encoder pins must be even");
constexpr uint8_t num_encoders = num_encoder_pins / 2;
constexpr uint8_t num_buttons = sizeof(button_pins) / sizeof(button_pins[0]);

#ifdef LED_BUILTIN
constexpr bool contains_led_builtin(const int pins[], uint8_t num_pins) {
    for (uint8_t i = 0; i < num_pins; i++) {
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
#endif /* LED_BUILTIN */

class EncoderMeta {
   public:
    Encoder *encoder = nullptr;
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
    // Loop through pairs of pins.
    for (uint8_t i = 0; i < num_encoders; i++) {
        uint8_t pin_a = encoder_pins[i * 2];
        uint8_t pin_b = encoder_pins[i * 2 + 1];
        // Encoder library does the pullups for us.
        encoders[i].encoder = new Encoder(pin_a, pin_b);
        // Just mirroring what the X-Touch Mini does.
        encoders[i].control = first_encoder_control_channel + i;
    }
    // For buttons, we need to do the pullups ourselves.
    for (uint8_t i = 0; i < num_buttons; i++) {
        buttons[i].button->attach(button_pins[i], INPUT_PULLUP);
        buttons[i].button->interval(5);
        buttons[i].note = first_button_note + i;
    }
    // Wait for the pullups to complete.
    // https://www.pjrc.com/teensy/td_digital.html says that this is plenty
    delayMicroseconds(10);
}

void check_encoder(EncoderMeta *encoder_meta) {
    // Use readAndReset() to always only get incremental changes.
    //
    // read() and write() both have to disable and enable interrupts, so this is
    // more efficient than calling read() and then write(0). Even if we only
    // call write(0) when read() returns a non-zero value, writing that zero
    // when reading is almost negligible compared to disabling and enabling
    // interrupts.
    int32_t rotation = encoder_meta->encoder->readAndReset();

    if (rotation == 0) {
        return;
    }
    uint32_t now = millis();
    uint32_t elapsed = millis() - encoder_meta->previous_millis;
    encoder_meta->previous_millis = now;
    if (elapsed < 100) {
        rotation *= (100 / elapsed);
    }
    // Encoders can be a bit chattery, so debounce. 2ms seems to work well.
    if (elapsed <= 2) {
        return;
    }
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
    usbMIDI.sendControlChange(encoder_meta->control, rotation, 1);
}

void check_button(ButtonMeta *button_meta) {
    button_meta->button->update();
    if (button_meta->button->pressed()) {
        usbMIDI.sendNoteOn(button_meta->note, 127, 1);
    } 
    if (button_meta->button->released()) {
        usbMIDI.sendNoteOff(button_meta->note, 0, 1);
    }
}

void loop() {
    for (uint8_t i = 0; i < num_encoders; i++) {
        check_encoder(&encoders[i]);
    }
    for (uint8_t i = 0; i < num_buttons; i++) {
        check_button(&buttons[i]);
    }
    // Consume all incoming MIDI messages to prevent hangups.
    while (usbMIDI.read()) {
    }
}