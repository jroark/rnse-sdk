#include "builtins.h"
#include "notebook.h"
#include "phone.h"
enum { BOOK, CONTACT, NAV, DESTINATION, TEL, CAR, EDIT, DELETE };
static int page, selection, back, entry, character, edit_target;
static char edit[24], dial[24];
static const char *message = "";
static u32 frame_count, last_ignition, last_phone, last_refresh, revision;
static int ignition, key_present, ignition_seen, phone_seen;
static const char alphabet[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-./+";
static const char digits[] = "0123456789+*#";
static const char coordinate_chars[] = "0123456789-.";
static int wrap(int x, int n) {
    if (x < 0) {
        int r = (int)umod(0u - (u32)x, (u32)n);
        return r ? n - r : 0;
    }
    return (int)umod((u32)x, (u32)n);
}
static void copy(char *to, const char *from, int n) {
    int i = 0;
    while (i < n - 1 && from[i]) {
        to[i] = from[i];
        i++;
    }
    to[i] = 0;
}
static int length(const char *s) {
    int i = 0;
    while (s[i])
        i++;
    return i;
}
static const char *chars(void) {
    return edit_target == 1 || edit_target == 5   ? digits
           : edit_target == 3 || edit_target == 4 ? coordinate_chars
                                                  : alphabet;
}
static void coordinate(char *b, s32 value) {
    u32 v = value < 0 ? 0u - (u32)value : (u32)value;
    int n;
    if (value < 0)
        *b++ = '-';
    ui_number(b, (int)udiv(v, 10000));
    n = length(b);
    b[n++] = '.';
    v = umod(v, 10000);
    b[n++] = '0' + udiv(v, 1000);
    b[n++] = '0' + umod(udiv(v, 100), 10);
    b[n++] = '0' + umod(udiv(v, 10), 10);
    b[n++] = '0' + umod(v, 10);
    b[n] = 0;
}
static int parse_coordinate(const char *s, int limit, s32 *out) {
    int negative = 0, whole = 0, fraction = 0, n = 0, digits_n = 0;
    if (*s == '-') {
        negative = 1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        whole = whole * 10 + *s++ - '0';
        if (whole > limit)
            return 0;
        digits_n++;
    }
    if (!digits_n)
        return 0;
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            if (n == 4)
                return 0;
            fraction = fraction * 10 + *s++ - '0';
            n++;
        }
    }
    if (*s)
        return 0;
    while (n++ < 4)
        fraction *= 10;
    if (whole == limit && fraction)
        return 0;
    *out = (whole * 10000 + fraction) * (negative ? -1 : 1);
    return 1;
}
static int valid_number(const char *s) {
    int i;
    if (!*s)
        return 0;
    for (i = 0; s[i]; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] == '+' && i == 0) ||
              s[i] == '*' || s[i] == '#'))
            return 0;
    return 1;
}
static void begin_edit(int target, const char *s, int previous) {
    edit_target = target;
    copy(edit, s, sizeof(edit));
    character = 0;
    back = previous;
    page = EDIT;
    message = "Turn: character / Press: append";
}
static void saved(void) {
    notebook_save();
    message = "Saved";
    console_puts("modern: notebook saved\n");
}
void builtin_init(void) { notebook_load(); }
int builtin_open(int key) {
    if (key == KEY_NAME)
        page = BOOK;
    else if (key == KEY_NAV)
        page = NAV;
    else if (key == KEY_TEL)
        page = TEL;
    else if (key == KEY_CAR)
        page = CAR;
    else
        return 0;
    selection = entry = 0;
    message = "";
    return 1;
}
void builtin_turn(int delta) {
    if (page == EDIT) {
        character = wrap(character + delta, length(chars()));
        return;
    }
    if (page == BOOK)
        selection = wrap(selection + delta, CONTACTS);
    if (page == NAV)
        selection = wrap(selection + delta, DESTINATIONS);
}
static void edit_save(void) {
    s32 value;
    int i;
    if (edit_target == 0)
        copy(contacts[entry].name, edit, 20);
    else if (edit_target == 1) {
        if (*edit && !valid_number(edit)) {
            message = "Use digits, leading +, * or #";
            return;
        }
        copy(contacts[entry].number, edit, 20);
    } else if (edit_target == 2)
        copy(destinations[entry].name, edit, 20);
    else if (edit_target == 3 || edit_target == 4) {
        if (!parse_coordinate(edit, edit_target == 3 ? 90 : 180, &value)) {
            message = "Invalid coordinate / 4 decimals max";
            return;
        }
        if (edit_target == 3)
            destinations[entry].lat = value;
        else
            destinations[entry].lon = value;
    } else if (edit_target == 5) {
        if (!valid_number(edit)) {
            message = "Enter a valid telephone number";
            return;
        }
        for (i = 0; i < 24; i++)
            dial[i] = edit[i];
        page = TEL;
        message = "Press Call to dial";
        return;
    }
    saved();
    page = back;
}
int builtin_key(int key) {
    char b[24];
    int n;
    if (key == KEY_RETURN) {
        if (page == EDIT) {
            page = back;
            message = "Edit cancelled";
            return 1;
        }
        if (page == DELETE) {
            page = back;
            message = "Delete cancelled";
            return 1;
        }
        if (page == CONTACT) {
            page = BOOK;
            selection = entry;
            return 1;
        }
        if (page == DESTINATION) {
            page = NAV;
            selection = entry;
            return 1;
        }
        return 0;
    }
    if (page == EDIT) {
        if (key == KEY_SELECT) {
            n = length(edit);
            if (n < (edit_target == 5 ? 23 : 19)) {
                edit[n] = chars()[character];
                edit[n + 1] = 0;
            } else
                message = "Field is full";
        }
        if (key == KEY_SOFT_TL) {
            n = length(edit);
            if (n)
                edit[n - 1] = 0;
        }
        if (key == KEY_SOFT_TR)
            edit[0] = 0;
        if (key == KEY_SOFT_BL) {
            page = back;
            message = "Edit cancelled";
        }
        if (key == KEY_SOFT_BR)
            edit_save();
        return 1;
    }
    if (page == BOOK && (key == KEY_SELECT || key == KEY_SOFT_BR)) {
        entry = selection;
        page = CONTACT;
    } else if (page == NAV && (key == KEY_SELECT || key == KEY_SOFT_BR)) {
        entry = selection;
        page = DESTINATION;
    } else if (page == CONTACT) {
        if (key == KEY_SOFT_TL)
            begin_edit(0, contacts[entry].name, CONTACT);
        if (key == KEY_SOFT_TR)
            begin_edit(1, contacts[entry].number, CONTACT);
        if (key == KEY_SOFT_BL) {
            back = CONTACT;
            page = DELETE;
        }
        if (key == KEY_SOFT_BR) {
            copy(dial, contacts[entry].number, sizeof(dial));
            page = TEL;
            message = "Press Call to dial";
        }
    } else if (page == DESTINATION) {
        if (key == KEY_SOFT_TL)
            begin_edit(2, destinations[entry].name, DESTINATION);
        if (key == KEY_SOFT_TR) {
            coordinate(b, destinations[entry].lat);
            begin_edit(3, b, DESTINATION);
        }
        if (key == KEY_SOFT_BR) {
            coordinate(b, destinations[entry].lon);
            begin_edit(4, b, DESTINATION);
        }
        if (key == KEY_SOFT_BL) {
            back = DESTINATION;
            page = DELETE;
        }
    } else if (page == DELETE) {
        if (key == KEY_SOFT_BR) {
            if (back == CONTACT) {
                Contact empty = {{0}, {0}};
                contacts[entry] = empty;
            } else {
                Destination empty = {{0}, 0, 0};
                destinations[entry] = empty;
            }
            saved();
            page = back;
        }
        if (key == KEY_SOFT_BL)
            page = back;
    } else if (page == TEL) {
        if (key == KEY_SOFT_TL)
            phone_connect();
        if (key == KEY_SOFT_TR)
            begin_edit(5, dial, TEL);
        if (key == KEY_SELECT || key == KEY_SOFT_BR) {
            int r = phone_ringing() ? phone_answer() : phone_dial(dial);
            message = r ? "Module not ready, busy or invalid number"
                        : "Request sent to factory module";
        }
        if (key == KEY_SOFT_BL) {
            message = phone_hangup() ? "No current call or module busy"
                                     : "End request sent";
        }
    } else if (page == CAR && key == KEY_SOFT_BR) {
        frame_count = 0;
        message = "Frame counter reset";
    }
    return 1;
}
int builtin_poll(void) {
    u16 id;
    u8 data[8];
    int n, changed = 0;
    u32 now = millis();
    while ((n = can_receive(&id, data)) >= 0) {
        frame_count++;
        if ((id == 0x271 || id == 0x2c3) && n >= 1) {
            ignition_seen = 1;
            ignition = !!(data[0] & 2);
            key_present = !!(data[0] & 1);
            last_ignition = now;
        }
        if (id == 0x665) {
            phone_seen = 1;
            last_phone = now;
        }
        phone_receive(id, data, n);
    }
    phone_poll();
    if (phone_revision() != revision) {
        revision = phone_revision();
        if (page == TEL)
            message = "";
        changed = 1;
    }
    if (now - last_refresh >= 1000) {
        last_refresh = now;
        if (page == CAR || page == TEL)
            changed = 1;
    }
    return changed;
}
void builtin_render(int volume) {
    char b[64];
    int i, start;
    const char *s;
    if (page == BOOK || page == NAV) {
        ui_frame(page == BOOK ? "Address book" : "Destinations",
                 page == BOOK ? "Local favourites" : "Saved coordinates", "",
                 "RETURN: back", "Open", volume);
        start = selection / 5 * 5;
        for (i = 0;
             i < 5 && start + i < (page == BOOK ? CONTACTS : DESTINATIONS);
             i++) {
            s = page == BOOK ? contacts[start + i].name
                             : destinations[start + i].name;
            ui_row(i, *s ? s : "Empty slot - press to edit",
                   start + i == selection);
        }
        if (page == NAV)
            ui_text(14, 190, "GPS and route guidance not available", 2, 1);
    } else if (page == CONTACT) {
        ui_frame("Contact", "Edit name", "Edit number", "Delete", "Use number",
                 volume);
        ui_text(16, 78,
                *contacts[entry].name ? contacts[entry].name
                                      : "Unnamed contact",
                1, 2);
        ui_text(16, 116,
                *contacts[entry].number ? contacts[entry].number
                                        : "No number saved",
                2, 2);
    } else if (page == DESTINATION) {
        ui_frame("Destination", "Edit name", "Latitude", "Delete", "Longitude",
                 volume);
        ui_text(16, 72,
                *destinations[entry].name ? destinations[entry].name
                                          : "Unnamed destination",
                1, 2);
        coordinate(b, destinations[entry].lat);
        ui_text(16, 103, "Lat", 2, 1);
        ui_text(60, 101, b, 1, 2);
        coordinate(b, destinations[entry].lon);
        ui_text(16, 132, "Lon", 2, 1);
        ui_text(60, 130, b, 1, 2);
        ui_text(16, 165, "Stored location / no route guidance", 2, 1);
    } else if (page == EDIT) {
        ui_frame(edit_target == 0   ? "Contact name"
                 : edit_target == 1 ? "Phone number"
                 : edit_target == 2 ? "Place name"
                 : edit_target == 3 ? "Latitude"
                 : edit_target == 4 ? "Longitude"
                                    : "Dial number",
                 "Backspace", "Clear", "Cancel", "Save", volume);
        ui_text(16, 79, *edit ? edit : "...", 1, 2);
        b[0] = chars()[character];
        b[1] = 0;
        screen_fill(16, 111, 36, 34, 3);
        ui_text(26, 120, b, 1, 2);
        ui_text(68, 121, "Turn / Press to append", 2, 1);
    } else if (page == DELETE) {
        ui_frame("Delete saved entry", "Confirmation", "", "Cancel", "Delete",
                 volume);
        ui_text(16, 91, "Delete this saved entry?", 1, 2);
        ui_text(16, 123, "This clears its local slot.", 2, 1);
    } else if (page == TEL) {
        ui_frame("Telephone", "Connect module", "Edit number", "End / Reject",
                 phone_ringing() ? "Answer" : "Call", volume);
        ui_text(16, 74, phone_status(), 1, 1);
        ui_text(16, 103,
                *phone_number() ? phone_number()
                : *dial         ? dial
                                : "Enter a number or use NAME",
                1, 2);
        ui_text(16, 143,
                phone_seen && millis() - last_phone < 3000
                    ? "Module present on vehicle bus"
                    : "Original Audi Bluetooth module",
                2, 1);
        ui_text(16, 164, "Call audio not yet supported", 2, 1);
    } else {
        ui_frame("Vehicle status", "Live infotainment CAN", "", "",
                 "Reset count", volume);
        ui_row(0,
               !ignition_seen || millis() - last_ignition > 1500
                   ? "Ignition: no recent data"
               : ignition ? "Ignition: on"
                          : "Ignition: off",
               0);
        ui_row(1,
               !ignition_seen || millis() - last_ignition > 1500
                   ? "Key: no recent data"
               : key_present ? "Key: inserted"
                             : "Key: removed",
               0);
        ui_number(b, (int)frame_count);
        ui_text(16, 120, "Received frames", 2, 1);
        ui_text(230, 116, b, 1, 2);
        ui_number(b, (int)can_lost());
        ui_text(16, 147, "Dropped frames", 2, 1);
        ui_text(230, 143, b, 1, 2);
        ui_text(16, 174, "No vehicle settings are changed", 2, 1);
    }
    if (page != NAV)
        ui_text(14, 190, message, 2, 1);
}
