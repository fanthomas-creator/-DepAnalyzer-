#ifndef PARSER_VUE_H
#define PARSER_VUE_H

#include "types.h"

/* Parse Vue Single File Components (.vue).
 * Structure:
 *   <template> ... </template>
 *   <script> or <script setup> ... </script>
 *   <style> or <style scoped> ... </style>
 *
 * Detects in <script>:
 *   import Foo from './Foo.vue'       → internal
 *   import { ref } from 'vue'         → external (vue)
 *   import axios from 'axios'         → external
 *   import './side-effect'            → internal/external
 *   defineProps / defineEmits / defineExpose → Vue 3 macros (calls)
 *   export default { components: {} } → component deps
 *
 * Detects in <template>:
 *   <MyComponent ... />               → component reference (call)
 *   <router-view> / <router-link>     → vue-router dep
 *
 * Detects in <style>:
 *   @import './base.css'              → internal CSS dep
 */
void parser_vue_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_VUE_H */
