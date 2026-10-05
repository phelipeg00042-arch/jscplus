#ifndef JSC_INTERN_H
#define JSC_INTERN_H

/* Intern de strings (otimizacao)
 * Strings internadas sao unicas no processo.
 * Comparacao por ponteiro == funciona.
 */

const char *intern(const char *s);
void intern_cleanup(void);

#endif
