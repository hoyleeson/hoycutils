#ifndef _COMMON_PARSER_H_
#define _COMMON_PARSER_H_

#define T_EOF 0
#define T_TEXT 1
#define T_NEWLINE 2

struct parse_state
{
    char *ptr;
    char *text;
    int line;
    int nexttoken;
    void *context;
    void (*parse_line)(struct parse_state *state, int nargs, char **args);
    const char *filename;
    void *priv;
};

int lookup_keyword(const char *s);
int next_token(struct parse_state *state);
void parse_error(struct parse_state *state, const char *fmt, ...);

#endif /* PARSER_H_ */
