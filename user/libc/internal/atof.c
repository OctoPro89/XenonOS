#include <xlibc/stdlib.h>
#include <xlibc/ctype.h>

// TODO: check
double atof(const char *s)
{
    double val, power;
    int i, sign, exp_sign, exp_val;

    for (i = 0; isspace(s[i]); i++);

    sign = (s[i] == '-') ? -1 : 1;
    if (s[i] == '+' || s[i] == '-') {
        i++;
    }

    for (val = 0.0; isdigit(s[i]); i++) {
        val = 10.0 * val + (s[i] - '0');
    }

    if (s[i] == '.') {
        i++;
    }
    for (power = 1.0; isdigit(s[i]); i++) {
        val = 10.0 * val + (s[i] - '0');
        power *= 10.0;
    }

    double result = sign * val / power;

    if (s[i] == 'e' || s[i] == 'E') {
        i++;
        
        exp_sign = (s[i] == '-') ? -1 : 1;
        if (s[i] == '+' || s[i] == '-') {
            i++;
        }
        
        for (exp_val = 0; isdigit(s[i]); i++) {
            exp_val = 10 * exp_val + (s[i] - '0');
        }
        
        double scale = 1.0;
        for (int j = 0; j < exp_val; j++) {
            scale *= 10.0;
        }
        
        if (exp_sign == 1) {
            result *= scale;
        } else {
            result /= scale;
        }
    }

    return result;
}