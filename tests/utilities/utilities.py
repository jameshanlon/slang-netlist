import re


def normalize_string(s: str) -> str:
    """
    Normalise a string by:
      1. Removing leading/trailing whitespace from each line.
      2. Ignoring lines that are empty or contain only whitespace.
      3. Collapsing all sequences of internal whitespace (spaces, tabs, newlines)
         into a single space character.
    """
    stripped_lines = [line.strip() for line in s.splitlines()]
    non_empty_lines = [line for line in stripped_lines if line]
    condensed_string = " ".join(non_empty_lines)

    # Collapse all internal sequences of whitespace.
    final_normalized = re.sub(r"\s+", " ", condensed_string)

    # Remove leading/trailing whitespace.
    return final_normalized.strip()


def fuzzy_compare_strings(str1: str, str2: str) -> bool:
    return normalize_string(str1) == normalize_string(str2)
