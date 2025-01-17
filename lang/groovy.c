/*
 * Groovy mode for QEmacs.
 *
 * Copyright (c) 2015-2020 Charlie Gordon.
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
#include "clang.h"

static const char groovy_keywords[] = {
    /* language specific keywords */
    "as|def|in|trait|"
    /* documented java keywords */
    "assert|break|case|catch|class|const|continue|"
    "default|do|else|enum|extends|final|finally|for|goto|"
    "if|implements|import|instanceof|interface|new|"
    "package|return|super|switch|"
    "this|throw|throws|try|while|"
    /* boolean and null literals */
    "false|null|true|"
    /* other java keywords */
    "abstract|native|private|protected|public|static|strictfp|"
    "synchronized|threadsafe|transient|volatile|"
};

static const char groovy_types[] = {
    "void|boolean|byte|char|short|int|long|double|float|"
};

enum {
    IN_GROOVY_COMMENT      = 0x01,
    IN_GROOVY_STRING       = 0x02,
    IN_GROOVY_STRING2      = 0x04,
    IN_GROOVY_LONG_STRING  = 0x08,
    IN_GROOVY_LONG_STRING2 = 0x10,
    IN_GROOVY_DOLLAR_STRING = 0x20,
};

enum {
    GROOVY_STYLE_TEXT =       QE_STYLE_DEFAULT,
    GROOVY_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    GROOVY_STYLE_COMMENT =    QE_STYLE_COMMENT,
    GROOVY_STYLE_STRING =     QE_STYLE_STRING,
    GROOVY_STYLE_DOLLAR_STRING = QE_STYLE_STRING,
    GROOVY_STYLE_REGEX =      QE_STYLE_STRING_Q,
    GROOVY_STYLE_NUMBER =     QE_STYLE_NUMBER,
    GROOVY_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    GROOVY_STYLE_TYPE =       QE_STYLE_TYPE,
    GROOVY_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    GROOVY_STYLE_ERROR =      QE_STYLE_ERROR,
};

static int qe_is_groovy_letter(int c) {
    return qe_isalpha_(c) ||
            (qe_inrange(c, 0x00C0, 0xFFFE) && c != 0x00D7 && c != 0x00F7);
}

static int java_scan_number(unsigned int *str0, int flavor)
{
    unsigned int *str = str0;
    int c = *str++;
    int octal = 0, nonoctal = 0, isfloat = 0;

    /* Number types:
     * binary integers: 0[bB][01]([01_]*[01])?[gliGLI]?
     * octal integers:  0([0-7_]*[0-7])?[gliGLI]?
     * hex integers:    0[xX][0-9a-zA-Z]([0-9a-zA-Z_]*[0-9a-zA-Z])?
     *                      [gliGLI]?
     * decimal integers: [1-9]([0-9_]*[0-9])?[gliGLI]?
     * decimal floats:  [0-9]([0-9_]*[0-9])?
     *                      [.]([0-9]([0-9_]*[0-9])?)?
     *                      ([eE][-+]?[0-9]([0-9_]*[0-9])?)?[dfDF]?
     *                  [.][0-9]([0-9_]*[0-9])?
     *                      ([eE][-+]?[0-9]([0-9_]*[0-9])?)?[dfDF]?
     *                  [0-9]([0-9_]*[0-9])?
     *                      [eE][-+]?[0-9]([0-9_]*[0-9])?[dfDF]?
     *                  [0-9]([0-9_]*[0-9])?[dfDF]
     * hex floats:      0[xX][0-9a-zA-Z]([0-9a-zA-Z_]*[0-9a-zA-Z])?
     *                      ([.]([0-9a-zA-Z]([0-9a-zA-Z_]*[0-9a-zA-Z])?)?)?
     *                      [pP][-+]?[0-9]([0-9_]*[0-9])?[dfDF]?
     *                  0[xX][.][0-9a-zA-Z]([0-9a-zA-Z_]*[0-9a-zA-Z])?
     *                      [pP][-+]?[0-9]([0-9_]*[0-9])?[dfDF]?
     *
     * Groovy requires a digit after the decimal point.
     *
     * This scanner is relaxed to allow for partial numbers at end of
     * line to avoid showing errors as numbers are typed.
     */
    if (c == '0') {
        if (qe_match2(*str, 'b', 'B')) {
            /* binary numbers */
            str++;
            if (!*str) goto done;
            if (!qe_isbindigit(*str)) goto error;
            for (str += 1; qe_isbindigit_(*str); str++)
                continue;
            if (!*str)  goto done;
            if (str[-1] == '_') goto error;
            if (qe_findchar("gliGLI", *str))
                str++;
            goto done;
        }
        if (qe_match2(*str, 'x', 'X')) {
            /* hexadecimal numbers */
            str++;
            if (!*str) goto done;
            if (*str != '.') {
                if (!qe_isxdigit(*str)) goto error;
                for (str += 1; qe_isxdigit_(*str); str++)
                    continue;
                if (!*str) goto done;
                if (str[-1] == '_') goto error;
                if (qe_findchar("gliGLI", *str)) {
                    str++;
                    goto done;
                }
            }
            if (qe_findchar(".pP", *str)) {
                isfloat = 1;
                if (*str == '.') {
                    if (str == str0 + 2 && !qe_isxdigit(str[1])) goto error;
                    if (flavor == CLANG_GROOVY && !qe_isxdigit(str[1])) goto done;
                    for (str += 1; qe_isxdigit_(*str); str++)
                        continue;
                }
                if (!*str) goto done;
                if (!qe_match2(*str, 'p', 'P')) goto error;
                str++;
                if (qe_match2(*str, '+', '-'))
                    str++;
                if (!*str) goto done;
                if (!qe_isdigit(*str)) goto error;
                for (str += 1; qe_isdigit_(*str); str++)
                    continue;
                if (str[-1] == '_') goto error;
            }
            if (qe_findchar("dfDF", *str))
                str++;
            goto done;
        }
        octal = 1;
    } else
    if (c == '.')
        str--;

    /* decimal and octal numbers */
    for (; qe_isdigit_(*str); str++) {
        nonoctal |= qe_match2(*str, '8', '9');
    }
    if (!*str) goto done;
    if (str[-1] == '_') goto error;
    if (*str == '.') {
        if (str == str0 && !qe_isdigit(str[1])) goto done;
        if (flavor == CLANG_GROOVY && !qe_isdigit(str[1])) goto done;
        str++;
        isfloat = 1;
        if (!*str) goto done;
        if (qe_isdigit(*str)) {
            for (str += 1; qe_isdigit_(*str); str++)
                continue;
            if (!*str) goto done;
            if (str[-1] == '_') goto error;
        }
    }
    if (qe_match2(*str, 'e', 'E')) {
        str++;
        isfloat = 1;
        if (qe_match2(*str, '+', '-'))
            str++;
        if (!*str) goto done;
        if (!qe_isdigit(*str)) goto error;
        for (str += 1; qe_isdigit_(*str); str++)
            continue;
        if (!*str) goto done;
        if (str[-1] == '_') goto error;
    }
    if (qe_findchar("dfDF", *str)) {
        str++;
        isfloat = 1;
        goto done;
    }
    if (!*str) goto done;
    if (!isfloat) {
        if (octal && nonoctal) goto error;
        if (qe_findchar("gliGLI", *str)) {
            str++;
            goto done;
        }
    }

done:
    if (!qe_isalnum_(*str)) {
        return str - str0;
    }

error:
    while (qe_isalnum_(*str))
        str++;
    return -(str - str0);
}

static void groovy_colorize_line(QEColorizeContext *cp,
                                 unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start = i, c, style, sep = 0, klen, haslower;
    int state = cp->colorize_state;
    char kbuf[64];

    /* all these states are exclusive */
    if (state & IN_GROOVY_COMMENT) {
        goto parse_comment;
    }
    if (state & IN_GROOVY_STRING) {
        sep = '\'';
        goto parse_string;
    }
    if (state & IN_GROOVY_STRING2) {
        sep = '\"';
        goto parse_string;
    }
    if (state & IN_GROOVY_LONG_STRING) {
        sep = '\'';
        goto parse_long_string;
    }
    if (state & IN_GROOVY_LONG_STRING2) {
        sep = '\"';
        goto parse_long_string;
    }
    if (state & IN_GROOVY_DOLLAR_STRING) {
        goto parse_dollar_string;
    }

    style = 0;
    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (start == 0 && str[i] == '!') {
                /* shebang line (should do this generically) */
                i = n;
                style = GROOVY_STYLE_PREPROCESS;
                break;
            }
            continue;

        case '~':
            while (qe_isblank(str[i]))
                i++;
            if (str[i] == '/') {
                /* parse slashy string as regex */
                sep = '/';
                start = i++;
                goto parse_string;
            }
            continue;

        case '/':
            if (str[i] == '*') {
                /* normal comment */
                i++;
            parse_comment:
                state |= IN_GROOVY_COMMENT;
                for (; i < n; i++) {
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        state &= ~IN_GROOVY_COMMENT;
                        break;
                    }
                }
                style = GROOVY_STYLE_COMMENT;
                break;
            } else
            if (str[i] == '/') {
                /* line comment */
                i = n;
                style = GROOVY_STYLE_COMMENT;
                break;
            }
            /* XXX: should handle slashy strings */
            continue;

        case '\'':
        case '\"':
            /* parse string const */
            i--;
            sep = str[i++];
            /* XXX: should colorize interpolated strings */
            if (str[i] == (unsigned int)sep && str[i + 1] == (unsigned int)sep) {
                /* long string */
                state |= (sep == '\"') ? IN_GROOVY_LONG_STRING2 :
                        IN_GROOVY_LONG_STRING;
                i += 2;
            parse_long_string:
                while (i < n) {
                    c = str[i++];
                    if (c == '\\') {
                        if (i < n) {
                            i += 1;
                        }
                    } else
                    if (c == sep && str[i] == (unsigned int)sep && str[i + 1] == (unsigned int)sep) {
                        i += 2;
                        state &= (sep == '\"') ? ~IN_GROOVY_LONG_STRING2 :
                                 ~IN_GROOVY_LONG_STRING;
                        break;
                    }
                }
            } else {
                state |= (sep == '\"') ? IN_GROOVY_STRING2 : IN_GROOVY_STRING;
            parse_string:
                while (i < n) {
                    c = str[i++];
                    if (c == '\\') {
                        if (i < n) {
                            i += 1;
                        }
                    } else
                    if (c == sep) {
                        state &= (sep == '\"') ? ~IN_GROOVY_STRING2 : ~IN_GROOVY_STRING;
                        break;
                    }
                }
            }
            style = GROOVY_STYLE_STRING;
            break;

        case '$':
            if (str[i] == '/') {
                /* $ slashy string */
                i++;
                state |= IN_GROOVY_DOLLAR_STRING;
            parse_dollar_string:
                while (i < n) {
                    c = str[i++];
                    if (c == '$') {
                        if (i < n) {
                            i += 1;
                        }
                    } else
                    if (c == '/' && str[i] == '$') {
                        state &= ~IN_GROOVY_DOLLAR_STRING;
                        i += 2;
                        break;
                    }
                }
                style = GROOVY_STYLE_DOLLAR_STRING;
                break;
            }
            goto hasname;

        case '@':
            if (qe_isalpha(str[i])) {
                while (qe_isalnum_(str[i]) || qe_is_groovy_letter(str[i]) || str[i] == '.')
                    i++;
                if (start == 0 || str[start - 1] != '.')
                    style = GROOVY_STYLE_PREPROCESS;
                break;
            }
            continue;

        case '.':
            if (!qe_isdigit(str[i]))
                continue;
            /* fallthru */

        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            i--;
            klen = java_scan_number(str + i, CLANG_GROOVY);
            if (klen > 0) {
                i += klen;
                style = GROOVY_STYLE_NUMBER;
                break;
            } else
            if (klen < 0) {
                /* Malformed number constants */
                i -= klen;
                style = GROOVY_STYLE_ERROR;
                break;
            }
            i++;
            continue;

        default:
            if (qe_is_groovy_letter(c)) {
            hasname:
                haslower = 0;
                klen = 0;
                kbuf[klen++] = c;
                for (; qe_isalnum_(str[i]) || qe_is_groovy_letter(str[i]); i++) {
                    haslower |= qe_islower(str[i]);
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = str[i];
                }
                kbuf[klen] = '\0';

                /* keywords are not recognised after '.',
                 * nor before a single '.' nor a map key indicator ':' */
                if ((start == 0 || str[start - 1] != '.')
                &&  (str[i] != '.' || str[i + 1] == '.')
                &&  str[i] != ':') {
                    if ((qe_isupper(c) && haslower && !check_fcall(str, i))
                    ||  strfind(syn->types, kbuf)) {
                        style = GROOVY_STYLE_TYPE;
                        break;
                    }
                    if (strfind(syn->keywords, kbuf)) {
                        style = GROOVY_STYLE_KEYWORD;
                        break;
                    }
                }
                if (check_fcall(str, i)) {
                    style = GROOVY_STYLE_FUNCTION;
                    break;
                }
                continue;
            }
            continue;
        }
        if (style) {
            SET_COLOR(str, start, i, style);
            style = 0;
        }
    }
    /* set style on eol char */
    SET_COLOR1(str, n, style);

    cp->colorize_state = state;
}

static ModeDef groovy_mode = {
    .name = "Groovy",
    .extensions = "groovy|gradle",
    .shell_handlers = "groovy",
    .colorize_func = groovy_colorize_line,
    .colorize_flags = CLANG_GROOVY,
    .keywords = groovy_keywords,
    .types = groovy_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

static int groovy_init(void)
{
    qe_register_mode(&groovy_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(groovy_init);
