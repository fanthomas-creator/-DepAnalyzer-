#ifndef PARSER_SVELTE_H
#define PARSER_SVELTE_H

#include "types.h"

/* Parse Svelte Single File Components (.svelte).
 * Structure:
 *   <script> or <script context="module"> ... </script>
 *   HTML template (rest of file)
 *   <style> ... </style>
 *
 * Detects in <script>:
 *   import Foo from './Foo.svelte'    → internal
 *   import { writable } from 'svelte/store' → external "svelte"
 *   import axios from 'axios'         → external
 *   export let propName               → prop declaration
 *
 * Detects in template:
 *   <Foo /> / <MyComponent>           → component reference
 *   <svelte:component this={...}>     → dynamic component
 *   {#await} / {#each} / {#if}        → control flow (ignored)
 *
 * Detects in <style>:
 *   @import './base.css'              → CSS dep
 */
void parser_svelte_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_SVELTE_H */
