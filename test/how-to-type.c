/*
 * Copyright © 2020 Ran Benita <ran@unusedvar.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <xkbcommon/xkbcommon.h>

#define ARRAY_SIZE(arr) ((sizeof(arr) / sizeof(*(arr))))

static void
usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s [-r <rules>] [-m <model>] "
            "[-l <layout>] [-v <variant>] [-o <options>] <unicode codepoint>\n",
            argv0);
    fprintf(stderr, "Pipe into `column -ts $'\\t'` for nicely aligned output.\n");
    exit(2);
}

int
main(int argc, char *argv[])
{
    int opt;
    const char *rules = NULL;
    const char *model = NULL;
    const char *layout_ = NULL;
    const char *variant = NULL;
    const char *options = NULL;
    int exit = EXIT_FAILURE;
    struct xkb_context *ctx = NULL;
    char *endp;
    long val;
    uint32_t codepoint;
    xkb_keysym_t keysym;
    int ret;
    char name[200];
    struct xkb_keymap *keymap = NULL;
    xkb_keycode_t min_keycode, max_keycode;
    xkb_mod_index_t num_mods;

    while ((opt = getopt(argc, argv, "r:m:l:v:o:")) != -1) {
        switch (opt) {
        case 'r':
            rules = optarg;
            break;
        case 'm':
            model = optarg;
            break;
        case 'l':
            layout_ = optarg;
            break;
        case 'v':
            variant = optarg;
            break;
        case 'o':
            options = optarg;
            break;
        default:
            usage(argv[0]);
        }
    }
    if (argc - optind != 1) {
        usage(argv[0]);
    }

    errno = 0;
    val = strtol(argv[optind], &endp, 0);
    if (errno != 0 || endp == argv[optind] || val < 0 || val > 0x10FFFF) {
        usage(argv[0]);
    }
    codepoint = (uint32_t) val;

    keysym = xkb_utf32_to_keysym(codepoint);
    if (keysym == XKB_KEY_NoSymbol) {
        fprintf(stderr, "Failed to convert codepoint to keysym\n");
        goto err;
    }

    ret = xkb_keysym_get_name(keysym, name, sizeof(name));
    if (ret < 0 || (size_t) ret >= sizeof(name)) {
        fprintf(stderr, "Failed to get name of keysym\n");
        goto err;
    }

    ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) {
        fprintf(stderr, "Failed to create XKB context\n");
        goto err;
    }

    struct xkb_rule_names names = {
        .rules = rules,
        .model = model,
        .layout = layout_,
        .variant = variant,
        .options = options,
    };
    keymap = xkb_keymap_new_from_names(ctx, &names,
                                       XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        fprintf(stderr, "Failed to create XKB keymap\n");
        goto err;
    }

    printf("keysym: %s (%#x)\n", name, keysym);
    printf("KEYCODE\tKEY NAME\tLAYOUT#\tLAYOUT NAME\tLEVEL#\tMODIFIERS\n");

    min_keycode = xkb_keymap_min_keycode(keymap);
    max_keycode = xkb_keymap_max_keycode(keymap);
    num_mods = xkb_keymap_num_mods(keymap);
    for (xkb_keycode_t keycode = min_keycode; keycode <= max_keycode; keycode++) {
        const char *key_name;
        xkb_layout_index_t num_layouts;

        key_name = xkb_keymap_key_get_name(keymap, keycode);
        if (!key_name) {
            continue;
        }

        num_layouts = xkb_keymap_num_layouts_for_key(keymap, keycode);
        for (xkb_layout_index_t layout = 0; layout < num_layouts; layout++) {
            const char *layout_name;
            xkb_level_index_t num_levels;

            layout_name = xkb_keymap_layout_get_name(keymap, layout);
            if (!layout_name) {
                layout_name = "?";
            }

            num_levels = xkb_keymap_num_levels_for_key(keymap, keycode, layout);
            for (xkb_level_index_t level = 0; level < num_levels; level++) {
                int num_syms;
                const xkb_keysym_t *syms;
                size_t num_masks;
                xkb_mod_mask_t masks[100];

                num_syms = xkb_keymap_key_get_syms_by_level(
                    keymap, keycode, layout, level, &syms
                );
                if (num_syms != 1) {
                    continue;
                }
                if (syms[0] != keysym) {
                    continue;
                }

                num_masks = xkb_keymap_key_get_mods_for_level(
                    keymap, keycode, layout, level, masks, ARRAY_SIZE(masks)
                );
                for (size_t i = 0; i < num_masks; i++) {
                    xkb_mod_mask_t mask = masks[i];

                    printf("%u\t%s\t%u\t%s\t%u\t[ ",
                           keycode, key_name, layout + 1, layout_name, level + 1);
                    for (xkb_mod_index_t mod = 0; mod < num_mods; mod++) {
                        if ((mask & (1 << mod)) == 0) {
                            continue;
                        }
                        printf("%s ", xkb_keymap_mod_get_name(keymap, mod));
                    }
                    printf("]\n");
                }
            }
        }
    }

    exit = EXIT_SUCCESS;
err:
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);
    return exit;
}
