/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>

#include <modvm/core/board.h>
#include <modvm/util/bug.h>
#include <modvm/util/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "board: " fmt

#define MAX_BOARD_DESCS 16

static const struct board_desc *board_descs[MAX_BOARD_DESCS];
static unsigned int nr_board_descs;

/**
 * board_register - register a board description
 * @desc: the board definition to expose to the system
 *
 * Typically invoked automatically via compiler constructor attributes
 * before the main routine executes.
 */
void board_register(const struct board_desc *desc)
{
	if (!desc || !desc->name || !*desc->name)
		panic(pr_fmt("invalid description\n"));

	for (unsigned int i = 0; i < nr_board_descs; i++) {
		if (!strcmp(board_descs[i]->name, desc->name))
			panic(pr_fmt("duplicate registration: %s\n"), desc->name);
	}

	if (nr_board_descs == MAX_BOARD_DESCS)
		panic(pr_fmt("registry full\n"));

	board_descs[nr_board_descs++] = desc;
}

/**
 * board_find - find a board description by name
 * @name: the string identifier of the board type
 *
 * Return: pointer to the board definition, or NULL if unsupported.
 */
const struct board_desc *board_find(const char *name)
{
	if (WARN_ON(!name))
		return NULL;

	for (unsigned int i = 0; i < nr_board_descs; i++) {
		if (!strcmp(board_descs[i]->name, name))
			return board_descs[i];
	}

	return NULL;
}
