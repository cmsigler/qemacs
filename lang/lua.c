/*
 * Lua language mode for QEmacs.
 *
 * Copyright (c) 2000-2020 Charlie Gordon.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "qe.h"

/*---------------- Lua script coloring ----------------*/

static char const lua_keywords[] = {
    "|and|break|do|else|elseif|end|false|for|function|goto|if|in"
    "|local|nil|not|or|repeat|require|return|then|true|until|while"
    "|self"
    "|"
};

#if 0
static char const lua_tokens[] = {
    "|+|-|*|/|%|^|#|==|~=|<=|>=|<|>|=|(|)|{|}|[|]|::|;|:|,|...|..|.|"
};
#endif

enum {
    IN_LUA_COMMENT = 0x10,
    IN_LUA_STRING  = 0x20,
    IN_LUA_STRING2 = 0x40,
    IN_LUA_LONGLIT = 0x80,
    IN_LUA_LEVEL   = 0x0F,
};

enum {
    LUA_STYLE_TEXT =     QE_STYLE_DEFAULT,
    LUA_STYLE_COMMENT =  QE_STYLE_COMMENT,
    LUA_STYLE_STRING =   QE_STYLE_STRING,
    LUA_STYLE_LONGLIT =  QE_STYLE_STRING,
    LUA_STYLE_NUMBER =   QE_STYLE_NUMBER,
    LUA_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    LUA_STYLE_FUNCTION = QE_STYLE_FUNCTION,
};

static int lua_long_bracket(unsigned int *str, int *level)
{
    int i;

    for (i = 1; str[i] == '='; i++)
        continue;
    if (str[i] == str[0]) {
        *level = i - 1;
        return 1;
    } else {
        return 0;
    }
}

static void lua_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, j, start = i, c, sep = 0, level = 0, level1, style;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_LUA_LONGLIT) {
        /* either a comment or a string */
        level = state & IN_LUA_LEVEL;
        goto parse_longlit;
    }

    if (state & IN_LUA_STRING) {
        sep = '\'';
        state = 0;
        goto parse_string;
    }
    if (state & IN_LUA_STRING2) {
        sep = '\"';
        state = 0;
        goto parse_string;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '-':
            if (str[i] == '-') {
                if (str[i + 1] == '['
                &&  lua_long_bracket(str + i + 1, &level)) {
                    state = IN_LUA_COMMENT | IN_LUA_LONGLIT |
                            (level & IN_LUA_LEVEL);
                    goto parse_longlit;
                }
                i = n;
                SET_COLOR(str, start, i, LUA_STYLE_COMMENT);
                continue;
            }
            break;
        case '\'':
        case '\"':
            /* parse string const */
            sep = c;
        parse_string:
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (str[i] == 'z' && i + 1 == n) {
                        /* XXX: partial support for \z */
                        state = (sep == '\'') ? IN_LUA_STRING : IN_LUA_STRING2;
                        i += 1;
                    } else
                    if (i == n) {
                        state = (sep == '\'') ? IN_LUA_STRING : IN_LUA_STRING2;
                    } else {
                        i += 1;
                    }
                } else
                if (c == sep) {
                    break;
                }
            }
            SET_COLOR(str, start, i, LUA_STYLE_STRING);
            continue;
        case '[':
            if (lua_long_bracket(str + i - 1, &level)) {
                state = IN_LUA_LONGLIT | (level & IN_LUA_LEVEL);
                goto parse_longlit;
            }
            break;
        parse_longlit:
            style = (state & IN_LUA_COMMENT) ?
                    LUA_STYLE_COMMENT : LUA_STYLE_LONGLIT;
            for (; i < n; i++) {
                if (str[i] == ']'
                &&  lua_long_bracket(str + i, &level1)
                &&  level1 == level) {
                    state = 0;
                    i += level + 2;
                    break;
                }
            }
            SET_COLOR(str, start, i, style);
            continue;
        default:
            if (qe_isdigit(c)) {
                /* XXX: should parse actual number syntax */
                for (; i < n; i++) {
                    if (!qe_isalnum(str[i]) && str[i] != '.')
                        break;
                }
                SET_COLOR(str, start, i, LUA_STYLE_NUMBER);
                continue;
            }
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf)) {
                    SET_COLOR(str, start, i, LUA_STYLE_KEYWORD);
                    continue;
                }
                for (j = i; j < n && qe_isspace(str[j]); j++)
                    continue;
                /* function calls use parenthesized argument list or
                   single string or table literal */
                if (qe_findchar("('\"{", str[j])) {
                    SET_COLOR(str, start, i, LUA_STYLE_FUNCTION);
                    continue;
                }
                continue;
            }
            break;
        }
    }
    cp->colorize_state = state;
}

static ModeDef lua_mode = {
    .name = "Lua",
    .extensions = "lua",
    .shell_handlers = "lua",
    .keywords = lua_keywords,
    .colorize_func = lua_colorize_line,
};

static int lua_init(void)
{
    qe_register_mode(&lua_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(lua_init);
