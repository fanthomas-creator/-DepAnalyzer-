#ifndef PARSER_TF_H
#define PARSER_TF_H

#include "types.h"

/* Parse HCL / Terraform files (.tf, .hcl).
 * Detects:
 *   module "name" { source = "./modules/vpc" }     → internal
 *   module "name" { source = "hashicorp/consul/aws" } → external
 *   provider "aws" { }                              → external dep
 *   resource "aws_instance" "web" { }               → FunctionIndex
 *   data "aws_ami" "ubuntu" { }                     → FunctionIndex
 *   variable "name" { }                             → FunctionIndex
 *   output "name" { }                               → FunctionIndex
 *   terraform { required_providers { ... } }        → external deps
 *   source = "registry.terraform.io/..."            → external
 *   locals { }                                      → local vars
 */
void parser_tf_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif
