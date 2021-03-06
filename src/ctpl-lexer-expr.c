/* 
 * 
 * Copyright (C) 2009-2011 Colomban Wendling <ban@herbesfolles.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include "ctpl-lexer-expr.h"
#include <string.h>
#include <errno.h>
#include <glib.h>
#include "ctpl-i18n.h"
#include "ctpl-lexer-private.h"
#include "ctpl-token.h"
#include "ctpl-token-private.h"
#include "ctpl-mathutils.h"
#include "ctpl-input-stream.h"
#include "ctpl-io.h"
#include "ctpl-value.h"


/**
 * SECTION: lexer-expr
 * @short_description: Syntax analyser for mathematical/test expressions
 * @include: ctpl/ctpl.h
 * 
 * Syntax analyser for mathematical or test expressions creating a token tree
 * from an expression.
 * 
 * To analyse an expression, use ctpl_lexer_expr_lex(). The resulting expression
 * should be freed with ctpl_token_expr_free() when no longer needed.
 * 
 * An expression is something like a mathematical expression that can include
 * references to variables. The allowed things are:
 * <variablelist>
 *   <varlistentry>
 *     <term>Binary operators</term>
 *     <listitem>
 *       <para>
 *         addition (<code>+</code>),
 *         subtraction (<code>-</code>),
 *         multiplication (<code>*</code>),
 *         division (<code>/</code>),
 *         modulo (<code>%</code>),
 *         and the boolean
 *         equality (<code>==</code>),
 *         non-equality (<code>!=</code>),
 *         inferiority (<code>&lt;</code>),
 *         inferiority-or-equality (<code>&lt;=</code>),
 *         superiority (<code>&gt;</code>),
 *         superiority-or-equality (<code>&gt;=</code>),
 *         AND (<code>&&</code>),
 *         and
 *         OR (<code>||</code>).
 *       </para>
 *       <para>
 *         The boolean operators results to the integer 0 if their expression
 *         evaluates to false, or to the positive integer 1 if their expression
 *         evaluates to true.
 *         This result might be used as a plain integer.
 *       </para>
 *       <para>
 *         The operators' priority is very common: boolean operators have the
 *         higher priority, followed by division, modulo and multiplication, and
 *         finally addition and subtraction which have the lower priority.
 *         When two operators have the same priority, the left one is prior over
 *         the right one.
 *       </para>
 *     </listitem>
 *   </varlistentry>
 *   <varlistentry>
 *     <term>Unary operators</term>
 *     <listitem>
 *       <para>
 *         The unary operators plus (<code>+</code>) and minus (<code>-</code>),
 *         that may precede any numeric operand.
 *       </para>
 *     </listitem>
 *   </varlistentry>
 *   <varlistentry>
 *     <term>Operands</term>
 *     <listitem>
 *       <para>
 *         Any numeric constant that ctpl_input_stream_read_number() supports,
 *         any reference to any
 *         <link linkend="ctpl-CtplEnviron">environment</link> variable,
 *         or any string literal that ctpl_input_stream_read_string_literal()
 *         supports.
 *         An operand may be suffixed with an index of the form
 *         <code>[&lt;expression&gt;]</code>.
 *       </para>
 *     </listitem>
 *   </varlistentry>
 *   <varlistentry>
 *     <term>Parentheses</term>
 *     <listitem>
 *       <para>
 *         Parentheses may be placed to delimit sub-expressions, allowing a fine
 *         control over operator priority.
 *       </para>
 *     </listitem>
 *   </varlistentry>
 * </variablelist>
 * 
 * <example>
 *   <title>A simple expression</title>
 *   <programlisting>
 *     42 * 2
 *   </programlisting>
 * </example>
 * <example>
 *   <title>A more complicated expression</title>
 *   <programlisting>
 *     (foo + 1) * 3 - 2 * bar
 *   </programlisting>
 * </example>
 * <example>
 *   <title>An expression with indexes</title>
 *   <programlisting>
 *     array[array[idx + 1]] * array[idx]
 *   </programlisting>
 * </example>
 * Of course, the latter examples supposes that the environment contains the
 * variables @foo, @bar, @array and @idx, and that they contains appropriate
 * values for latter evaluation.
 */


typedef struct _LexerExprState LexerExprState;

struct _LexerExprState
{
  gboolean  lex_all;  /* character ending the stream to lex, or 0 for none */
  guint     depth;    /* current parenthesis depth */
};


/*<standard>*/
GQuark
ctpl_lexer_expr_error_quark (void)
{
  static GQuark error_quark = 0;
  
  if (G_UNLIKELY (error_quark == 0)) {
    error_quark = g_quark_from_static_string ("CtplLexerExpr");
  }
  
  return error_quark;
}


/* @stream: #CtplInputStream holding the number.
 * @error: return location for errors, or %NULL to ignore them
 * See ctpl_input_stream_read_number()
 * Returns: A new #CtplTokenExpr or %NULL if the input doesn't contain any valid
 *          number.
 */
static CtplTokenExpr *
read_number (CtplInputStream *stream,
             GError         **error)
{
  CtplTokenExpr  *token = NULL;
  CtplValue       value;
  
  ctpl_value_init (&value);
  if (ctpl_input_stream_read_number (stream, &value, error)) {
    token = ctpl_token_expr_new_value (&value);
  }
  ctpl_value_free_value (&value);
  
  return token;
}

/* Reads a symbol from @stream
 * See ctpl_input_stream_read_symbol()
 * Returns: A new #CtplTokenExpr holding the symbol, or %NULL on error */
static CtplTokenExpr *
read_symbol (CtplInputStream *stream,
             GError         **error)
{
  CtplTokenExpr *token = NULL;
  gchar         *symbol;
  
  symbol = ctpl_input_stream_read_symbol (stream, error);
  if (symbol) {
    if (*symbol) {
      token = ctpl_token_expr_new_symbol (symbol, -1);
    } else {
      ctpl_input_stream_set_error (stream, error, CTPL_LEXER_EXPR_ERROR,
                                   CTPL_LEXER_EXPR_ERROR_SYNTAX_ERROR,
                                   _("No valid symbol"));
    }
  }
  g_free (symbol);
  
  return token;
}

/* Reads a string literal from @stream
 * See ctpl_input_stream_read_string_literal()
 * Returns: A new #CtplTokenExpr holding the string, or %NULL on error */
static CtplTokenExpr *
read_string_literal (CtplInputStream *stream,
                     GError         **error)
{
  CtplTokenExpr *token = NULL;
  gchar         *string;
  
  string = ctpl_input_stream_read_string_literal (stream, error);
  if (string) {
    CtplValue value;
    
    ctpl_value_init (&value);
    ctpl_value_set_string (&value, string);
    token = ctpl_token_expr_new_value (&value);
    ctpl_value_free_value (&value);
  }
  g_free (string);
  
  return token;
}

/* 
 * operators_array:
 * 
 * List of operators, with their priority and representation in the CTPL
 * language.
 * Don't forget to update this when adding an operator */
/* This is ordered alphabetically but tweaked for the string representations to be
 * match-able in order. E.g. >= must come before > not for > to match >=.
 * 
 * The order must also be the same as the CtplOperator enum, so
 * operators_array[i].op == i */
static const struct {
  CtplOperator  op;       /* The operator ID */
  guint         priority; /* Op's priority: the higher the prior */
  const gchar  *str;      /* Its string representation */
  gsize         str_len;  /* Cached length of @str */
} operators_array[] = {
  { CTPL_OPERATOR_AND,    30, "&&", 2 },
  { CTPL_OPERATOR_DIV,    20, "/",  1 },
  { CTPL_OPERATOR_EQUAL,  30, "==", 2 },
  { CTPL_OPERATOR_INFEQ,  30, "<=", 2 },
  { CTPL_OPERATOR_INF,    30, "<",  1 },
  { CTPL_OPERATOR_MINUS,  10, "-",  1 },
  { CTPL_OPERATOR_MODULO, 20, "%",  1 },
  { CTPL_OPERATOR_MUL,    20, "*",  1 },
  { CTPL_OPERATOR_NEQ,    30, "!=", 2 },
  { CTPL_OPERATOR_OR,     30, "||", 2 },
  { CTPL_OPERATOR_PLUS,   10, "+",  1 },
  { CTPL_OPERATOR_SUPEQ,  30, ">=", 2 },
  { CTPL_OPERATOR_SUP,    30, ">",  1 },
  /* must be last */
  { CTPL_OPERATOR_NONE,   0,  "not an operator", 15 }
};
/* number of true operators, without the NONE at the end */
static const gsize operators_array_length = G_N_ELEMENTS (operators_array) - 1;
/* the maximum length of a valid operator */
#define            OPERATORS_STR_MAXLEN     (2)


/* Gets whether op1 has priority over op2.
 * If both operators have the same priority, returns %TRUE */
static gboolean
operator_is_prior (CtplOperator op1,
                   CtplOperator op2)
{
  return operators_array[op1].priority >= operators_array[op2].priority;
}

/*
 * ctpl_operator_to_string:
 * @op: A #CtplOperator
 * 
 * Gets the string representation of an operator.
 * This representation is understood by the lexer if @op is valid.
 * 
 * Returns: A string representing the operator. This string should not be
 *          modified or freed.
 */
const gchar *
ctpl_operator_to_string (CtplOperator op)
{
  g_return_val_if_fail (op >= 0 && op <= CTPL_OPERATOR_NONE, NULL);
  
  return operators_array[op].str;
}

/*
 * ctpl_operator_from_string:
 * @str: A string starting with an operator
 * @len: length to read from @str, or -1 to read the whole string.
 * @operator_len: (out) (allow-none): Return location for the length of the read
 *                                    operator, or %NULL.
 * 
 * Tries to convert a string to an operator, as the lexer may do.
 * 
 * Returns: The read operator or %CTPL_OPERATOR_NONE if none successfully read.
 */
CtplOperator
ctpl_operator_from_string (const gchar *str,
                           gssize       len,
                           gsize       *operator_len)
{
  CtplOperator  op = CTPL_OPERATOR_NONE;
  gsize         i;
  gsize         length;
  
  length = (len < 0) ? strlen (str) : (gsize)len;
  for (i = 0; op == CTPL_OPERATOR_NONE &&
              i < operators_array_length; i++) {
    if (length >= operators_array[i].str_len &&
        strncmp (operators_array[i].str, str,
                 operators_array[i].str_len) == 0) {
      op = operators_array[i].op;
      if (operator_len) *operator_len = operators_array[i].str_len;
    }
  }
  
  return op;
}

/* Gets a human-readable name of the token's operator */
static const gchar *
token_operator_to_string (CtplTokenExpr *token)
{
  const gchar *str;
  
  /* by default, index the last op (error) */
  str = operators_array[operators_array_length].str;
  if (token->type == CTPL_TOKEN_EXPR_TYPE_OPERATOR) {
    str = ctpl_operator_to_string (token->token.t_operator->operator);
  }
  
  return str;
}

/*
 * validate_token_list:
 * @stream: The #CtplInputStream from where tokens comes (for error reporting)
 * @tokens: The list of tokens to validate
 * @error: Return location for an error, or %NULL to ignore them
 * 
 * Builds a #CtplTokenExpr from a list. It computes the priority of operators
 * when needed and builds a single fully valid root token linking the others.
 * If checks whether the token list is meaningful, e.g. that each binary
 * operator have two operands and so.
 * 
 * Note that this function relies on the token list to be valid; the only thing
 * that may be wrong is the last token being a operator, then missing it right
 * operand.
 * 
 * Returns: A new #CtplTokenExpr or %NULL on error.
 */
static CtplTokenExpr *
validate_token_list (CtplInputStream *stream,
                     GSList          *tokens,
                     GError         **error)
{
  CtplTokenExpr  *expr = NULL;
  CtplTokenExpr  *operands[2] = {NULL, NULL};
  CtplTokenExpr  *operators[2] = {NULL, NULL};
  gint            opt = 0;
  gint            opd = 0;
  gint            i = 0;
  GSList         *last_opd = NULL;
  
  /* we can assume the token list is fully valid, never having invalid token
   * suits as the caller have checked it, except for the last token that may be
   * an operator (then which misses its right operand) */
  for (; tokens; tokens = tokens->next) {
    if ((i++ % 2) == 0) {
      operands[opd++] = tokens->data;
      last_opd = tokens;
    } else {
      operators[opt++] = tokens->data;
      if (opt > 1) {
        if (operator_is_prior (operators[0]->token.t_operator->operator,
                               operators[1]->token.t_operator->operator)) {
          CtplTokenExpr *nexpr;
          
          nexpr = operators[0];
          operators[0] = operators[1];
          operators[1] = NULL;
          opt --;
          nexpr->token.t_operator->loperand = operands[0];
          nexpr->token.t_operator->roperand = operands[1];
          operands[0] = nexpr;
          operands[1] = NULL;
          opd = 1;
        } else {
          operands[1] = validate_token_list (stream, last_opd, error);
          opt --;
          
          break;
        }
      }
    }
  }
  if (opt == 0 && opd == 1) {
    /* nothing to do, just return the operand */
    expr = operands[0];
  } else if (opt == 1 && opd == 2) {
    CtplTokenExpr *nexpr;
    
    nexpr = operators[0];
    nexpr->token.t_operator->loperand = operands[0];
    nexpr->token.t_operator->roperand = operands[1];
    expr = nexpr;
  } else {
    /* even though the location reported by ctpl_input_stream_set_error() may
     * not be perfectly exact, it is probably better with it than without */
    ctpl_input_stream_set_error (stream, error, CTPL_LEXER_ERROR,
                                 CTPL_LEXER_EXPR_ERROR_MISSING_OPERAND,
                                 _("Too few operands for operator '%s'"),
                                 token_operator_to_string (operators[opt - 1]));
  }
  
  return expr;
}

static gboolean
lex_operand_index (CtplInputStream *stream,
                   CtplTokenExpr   *operand,
                   GError         **error)
{
  gboolean success = TRUE;
  
  /* if we have something that looks like an index, try to read it */
  while (success && ctpl_input_stream_skip_blank (stream, error) >= 0 &&
         ctpl_input_stream_peek_c (stream, NULL) == '[') {
    CtplTokenExpr  *idx;
    
    success = FALSE;
    ctpl_input_stream_get_c (stream, NULL); /* eat the [ */
    idx = ctpl_lexer_expr_lex_full (stream, FALSE, error);
    if (idx) {
      GError *err = NULL;
      gchar   c;
      
      c = ctpl_input_stream_get_c (stream, &err);
      if (err || c != ']') {
        if (err) {
          g_propagate_error (error, err);
        } else {
          ctpl_input_stream_set_error (stream, error, CTPL_LEXER_EXPR_ERROR,
                                       CTPL_LEXER_EXPR_ERROR_SYNTAX_ERROR,
                                       _("Unexpected character '%c', expected "
                                         "index end"), c);
        }
        ctpl_token_expr_free (idx);
      } else {
        operand->indexes = g_slist_append (operand->indexes, idx);
        success = TRUE;
      }
    }
  }
  
  return success;
}

/* Reads an operand.
 * Returns: A new #CtplTokenExpr on success, %NULL on error. */
static CtplTokenExpr *
lex_operand (CtplInputStream *stream,
             GError         **error)
{
  CtplTokenExpr  *token = NULL;
  gssize          read_size;
  gchar           buf[2] = {0, 0};
  
  read_size = ctpl_input_stream_peek (stream, buf, 2, error);
  if (read_size >= 0) {
    gchar c      = buf[0];
    gchar next_c = buf[1];
    
    if (g_ascii_isdigit (c) ||
        (c == '.' && g_ascii_isdigit (next_c)) ||
        c == '+' || c == '-') {
      token = read_number (stream, error);
    } else if (ctpl_is_symbol (c)) {
      token = read_symbol (stream, error);
    } else if (c == CTPL_STRING_DELIMITER_CHAR) {
      token = read_string_literal (stream, error);
    } else {
      ctpl_input_stream_set_error (stream, error, CTPL_LEXER_EXPR_ERROR,
                                   CTPL_LEXER_EXPR_ERROR_SYNTAX_ERROR,
                                   _("No valid operand at start of expression"));
    }
    if (token && ! lex_operand_index (stream, token, error)) {
      ctpl_token_expr_free (token); token = NULL;
    }
  }
  
  return token;
}

/* Reads an operator.
 * Returns: A new #CtplTokenExpr on success, %NULL on error. */
static CtplTokenExpr *
lex_operator (CtplInputStream *stream,
              GError         **error)
{
  CtplTokenExpr  *token   = NULL;
  gsize           off     = 0;
  CtplOperator    op      = CTPL_OPERATOR_NONE;
  gchar           buf[OPERATORS_STR_MAXLEN];
  gssize          read_size;
  
  read_size = ctpl_input_stream_peek (stream, buf, sizeof buf, error);
  if (read_size >= 0) {
    op = ctpl_operator_from_string (buf, read_size, &off);
    if (op == CTPL_OPERATOR_NONE) {
      ctpl_input_stream_set_error (stream, error, CTPL_LEXER_EXPR_ERROR,
                                   CTPL_LEXER_EXPR_ERROR_MISSING_OPERATOR,
                                   _("No valid operator"));
    } else {
      if (ctpl_input_stream_skip (stream, off, error) >= 0) {
        token = ctpl_token_expr_new_operator (op, NULL, NULL);
      }
    }
  }
  
  return token;
}

/* Recursive part of the lexer (does all but doesn't validates some parts). */
static CtplTokenExpr *
ctpl_lexer_expr_lex_internal (CtplInputStream  *stream,
                              LexerExprState   *state,
                              GError          **error)
{
  CtplTokenExpr  *expr_tok = NULL;
  GSList         *tokens = NULL;
  GError         *err = NULL;
  gboolean        expect_operand = TRUE;
  
  if (ctpl_input_stream_skip_blank (stream, error) >= 0) {
    while (! ctpl_input_stream_eof (stream, &err) && ! err) {
      CtplTokenExpr  *token = NULL;
      gchar           c;
      
      c = ctpl_input_stream_peek_c (stream, &err);
      if (err) {
        /* I/O error */
      } else {
        if (c == ')') {
          if (state->depth <= 0) {
            if (state->lex_all) {
              /* if we validate all, throw an error */
              ctpl_input_stream_set_error (stream, &err, CTPL_LEXER_EXPR_ERROR,
                                           CTPL_LEXER_EXPR_ERROR_SYNTAX_ERROR,
                                           _("Too many closing parenthesis"));
            }
            /* else, just stop lexing */
          } else {
            state->depth --;
            ctpl_input_stream_get_c (stream, &err); /* skip parenthesis */
          }
          /* stop lexing */
          break;
        } else {
          if (expect_operand) {
            /* try to read an operand */
            if (c == '(') {
              LexerExprState substate;
              
              ctpl_input_stream_get_c (stream, &err); /* skip parenthesis */
              if (! err) {
                substate = *state;
                substate.depth ++;
                token = ctpl_lexer_expr_lex_internal (stream, &substate, &err);
                if (token && substate.depth != state->depth) {
                  ctpl_input_stream_set_error (stream, &err,
                                               CTPL_LEXER_EXPR_ERROR,
                                               CTPL_LEXER_EXPR_ERROR_SYNTAX_ERROR,
                                               _("Missing closing parenthesis"));
                }
              }
            } else {
              token = lex_operand (stream, &err);
            }
          } else {
            /* try to read an operator */
            token = lex_operator (stream, &err);
          }
        }
        if (token) {
          expect_operand = ! expect_operand;
          tokens = g_slist_prepend (tokens, token);
        } else if (! state->lex_all) {
          if (err->domain != CTPL_IO_ERROR) {
            /* if we don't validate all, we don't want to throw an error when no
             * token was read, just stop lexing. */
            g_clear_error (&err);
          }
          break;
        }
        /* skip blank chars */
        ctpl_input_stream_skip_blank (stream, &err);
      }
    }
    if (! err) {
      if (! tokens) {
        /* if no tokens were read, complain */
        ctpl_input_stream_set_error (stream, &err, CTPL_LEXER_EXPR_ERROR,
                                     CTPL_LEXER_EXPR_ERROR_FAILED,
                                     _("No valid operand at start of expression"));
      } else {
        /* here check validity of token list, then create the final token. */
        tokens = g_slist_reverse (tokens);
        expr_tok = validate_token_list (stream, tokens, &err);
      }
    }
    if (err) {
      GSList *tmp;
      
      for (tmp = tokens; tmp; tmp = tmp->next) {
        /* don't free possible child tokens since we free each token
         * separately */
        ctpl_token_expr_free_full (tmp->data, FALSE);
      }
      g_propagate_error (error, err);
    }
    g_slist_free (tokens);
  }
  
  return expr_tok;
}


/**
 * ctpl_lexer_expr_lex:
 * @stream: A #CtplInputStream from where read the expression
 * @error: Return location for errors, or %NULL to ignore them.
 * 
 * Tries to lex the expression in @stream.
 * If you want to lex a #CtplInputStream that (may) hold other data after the
 * expression, see ctpl_lexer_expr_lex_full().
 * 
 * Returns: A new #CtplTokenExpr or %NULL on error.
 */
CtplTokenExpr *
ctpl_lexer_expr_lex (CtplInputStream *stream,
                     GError         **error)
{
  return ctpl_lexer_expr_lex_full (stream, TRUE, error);
}

/**
 * ctpl_lexer_expr_lex_full:
 * @stream: A #CtplInputStream
 * @lex_all: Whether to lex @stream until EOF or until the end of a valid
 *           expression. This is useful for expressions inside other data.
 * @error: Return location for errors, or %NULL to ignore them.
 * 
 * Tries to lex the expression in @stream.
 * 
 * Returns: A new #CtplTokenExpr or %NULL on error.
 */
CtplTokenExpr *
ctpl_lexer_expr_lex_full (CtplInputStream *stream,
                          gboolean         lex_all,
                          GError         **error)
{
  LexerExprState  state = {TRUE, 0};
  CtplTokenExpr  *expr_tok;
  GError         *err = NULL;
  
  state.lex_all = lex_all;
  expr_tok = ctpl_lexer_expr_lex_internal (stream, &state, &err);
  if (! err) {
    /* don't report an error if one already set */
    if (state.lex_all && ! ctpl_input_stream_eof (stream, &err)) {
      /* if we lex all and we don't have reached EOF here, complain */
      ctpl_input_stream_set_error (stream, &err, CTPL_LEXER_EXPR_ERROR,
                                   CTPL_LEXER_EXPR_ERROR_SYNTAX_ERROR,
                                   _("Trash data at end of expression"));
    }
  }
  if (err) {
    ctpl_token_expr_free (expr_tok);
    expr_tok = NULL;
    g_propagate_error (error, err);
  }
  
  return expr_tok;
}

/**
 * ctpl_lexer_expr_lex_string:
 * @expr: An expression
 * @len: Length of @expr or -1 to read the whole string
 * @error: Return location for errors, or %NULL to ignore them
 * 
 * Tries to lex the expression in @expr.
 * See ctpl_lexer_expr_lex().
 * 
 * Returns: A new #CtplTokenExpr or %NULL on error.
 */
CtplTokenExpr *
ctpl_lexer_expr_lex_string (const gchar *expr,
                            gssize       len,
                            GError     **error)
{
  CtplTokenExpr    *token = NULL;
  CtplInputStream  *stream;
  
  stream = ctpl_input_stream_new_for_memory (expr, len, NULL, NULL);
  token = ctpl_lexer_expr_lex (stream, error);
  ctpl_input_stream_unref (stream);
  
  return token;
}
