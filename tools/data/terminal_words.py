"""Existing bigram policy shared by the formal conversion tool."""
import sys
sys.dont_write_bytecode=True
TERMINAL_WORDS = frozenset({"的", "了", "是"})
