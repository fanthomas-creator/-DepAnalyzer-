#ifndef PARSER_SOL_H
#define PARSER_SOL_H
#include "types.h"
/* Parse Solidity smart contract files (.sol).
 * import "./interfaces/IERC20.sol"        → internal
 * import "@openzeppelin/contracts/..."    → external
 * import "hardhat/console.sol"            → external
 * contract MyToken is ERC20, Ownable     → def + deps
 * interface IERC20 { }                   → def
 * library SafeMath { }                   → def
 * abstract contract Base { }             → def
 * function transfer(...)                 → FunctionIndex
 * event Transfer(...)                    → FunctionIndex
 * modifier onlyOwner()                   → FunctionIndex
 * using SafeMath for uint256             → dep
 */
void parser_sol_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);
#endif
