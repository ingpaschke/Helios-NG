/* German Atari ST physical layout. Andre's STLOCAL.C handle_key() sends
 * the IKBD scan code unchanged (release is carried separately in What).
 * Character positions checked against EmuTOS bios/keyb_de.h normal/shift
 * tables; values here are X keysyms, not Atari character-set bytes.
 * Keep the recovered ST function keys, modifiers, arrows and keypad. */
static void germanSTKeymap(KeySymsPtr syms)
{
    static const struct { int scan; KeySym normal, shifted; } changes[] = {
        {0x04, XK_3, XK_section},
        {0x07, XK_6, XK_ampersand},
        {0x08, XK_7, XK_slash},
        {0x09, XK_8, XK_parenleft},
        {0x0a, XK_9, XK_parenright},
        {0x0b, XK_0, XK_equal},
        {0x0c, XK_ssharp, XK_question},
        {0x0d, XK_apostrophe, XK_grave},
        {0x15, XK_z, XK_Z},
        {0x1a, XK_udiaeresis, XK_Udiaeresis},
        {0x1b, XK_plus, XK_asterisk},
        {0x27, XK_odiaeresis, XK_Odiaeresis},
        {0x28, XK_adiaeresis, XK_Adiaeresis},
        {0x29, XK_numbersign, XK_asciicircum},
        {0x2b, XK_asciitilde, XK_bar},
        {0x2c, XK_y, XK_Y},
        {0x33, XK_comma, XK_semicolon},
        {0x34, XK_period, XK_colon},
        {0x35, XK_minus, XK_underscore},
        {0x60, XK_less, XK_greater}
    };
    int i, offset, width = syms->mapWidth;
    /* Shipped Helios X.library's XTranslateKey uses syms[ffs(state)]
     * for unshifted modifiers: Control selects column 3, Alt column 4.
     * Keep the five legacy columns; a two-column map reads the next key.
     * XTranslateKeySym subsequently applies the Control ASCII conversion. */
    for (i = 0; i <= syms->maxKeyCode - syms->minKeyCode; ++i) {
        syms->map[i * width + 3] = syms->map[i * width];
        syms->map[i * width + 4] = syms->map[i * width];
    }
    for (i = 0; i < sizeof(changes) / sizeof(changes[0]); ++i) {
        offset = (changes[i].scan + 7 - syms->minKeyCode) * width;
        syms->map[offset] = changes[i].normal;
        syms->map[offset + 1] = changes[i].shifted;
        syms->map[offset + 2] =
            (changes[i].normal == XK_z || changes[i].normal == XK_y ||
             changes[i].normal == XK_adiaeresis || changes[i].normal == XK_odiaeresis ||
             changes[i].normal == XK_udiaeresis) ? changes[i].shifted : changes[i].normal;
        syms->map[offset + 3] = changes[i].normal;
        syms->map[offset + 4] = changes[i].normal;
    }
}
