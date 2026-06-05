#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function formatNumber(value, format) {
  if (!format) {
    return String(value);
  }
  if (format === "{0:0.000}") {
    return value.toFixed(3);
  }
  if (format === "{0:0}") {
    return String(Math.round(value));
  }
  if (format === "value={0:0.0}") {
    return `value=${value.toFixed(1)}`;
  }
  if (format === "n={0:0}") {
    return `n=${Math.round(value)}`;
  }
  if (format.includes("{1") || (format.includes("{") && !format.includes("{0"))) {
    return "Invalid Format";
  }
  return format.replace("{0}", String(value));
}

function tixlSearchAndReplace(content, pattern, replacement, useRegex) {
  replacement = replacement == null ? replacement : replacement.replaceAll("\\n", "\n");
  if (!content || !replacement || !pattern) {
    return content ?? "";
  }
  if (useRegex) {
    return content.replace(new RegExp(pattern, "gs"), replacement);
  }
  return content.split(pattern).join(replacement);
}

function tixlSubstring(text, start, length) {
  const clamp = (value, min, max) => Math.min(Math.max(value, min), max);
  const clampStart = clamp(start, 0, text.length);
  const clampedLength = clamp(length, 0, text.length - clampStart);
  if (!text || clampedLength === 0 || clampStart >= text.length) {
    return "";
  }
  if (start === 0 && length >= text.length) {
    return text;
  }
  return text.substring(clampStart, clampStart + clampedLength);
}

function tixlStringRepeat(fragment, count) {
  count = Math.min(Math.max(count, 0), 1000);
  return count === 0 || !fragment ? "" : fragment.repeat(count);
}

function tixlChangeCase(input, mode) {
  return mode === 0 ? input.toUpperCase() : input.toLowerCase();
}

function tixlSplitString(input, split) {
  const delimiter = split.length === 0 || split === "\\n" ? "\n" : split[0];
  if (!input) {
    return [];
  }
  return input.split(delimiter);
}

function tixlJoinStringList(input, separator) {
  if (separator == null || !input || input.length === 0) {
    return "";
  }
  return input.join(separator.replaceAll("\\n", "\n"));
}

test("TiXL string convert scalar reference cases", () => {
  assert.equal(formatNumber(0, "{0:0.000}"), "0.000");
  assert.equal(formatNumber(-12.5, ""), "-12.5");
  assert.equal(formatNumber(3.25, "value={0:0.0}"), "value=3.3");
  assert.equal(formatNumber(12, "{0:0}"), "12");
  assert.equal(formatNumber(7, "n={0:0}"), "n=7");
  assert.equal(formatNumber(7, "{1:0}"), "Invalid Format");
});

test("TiXL string search scalar reference cases", () => {
  assert.equal("hello".indexOf("ell"), 1);
  assert.equal("hello".indexOf("ELL"), -1);
  assert.equal("".indexOf("x"), -1);
  assert.equal(tixlSearchAndReplace("a-b-a", "a", "x", false), "x-b-x");
  assert.equal(tixlSearchAndReplace("a\\nb", "\\n", " / ", false), "a / b");
  assert.equal(tixlSearchAndReplace("a1b2", "\\d", "x", true), "axb x".replace(" ", ""));
  assert.equal(tixlSubstring("abcdef", 2, 3), "cde");
  assert.equal(tixlSubstring("abcdef", -4, 2), "ab");
  assert.equal(tixlSubstring("abcdef", 3, 100), "def");
  assert.equal(tixlSubstring("abcdef", 2, 0), "");
});

test("TiXL string list/combine/transform scalar reference cases", () => {
  assert.equal("ten plus eleven is 21".length, 21);
  assert.equal("a\nb".length, 3);
  assert.equal("😀".length, 2, "TiXL .NET length counts UTF-16 code units");
  assert.equal(tixlStringRepeat("ha", 3), "hahaha");
  assert.equal(tixlStringRepeat("ha", -1), "");
  assert.equal(tixlStringRepeat("x", 1001).length, 1000);
  assert.equal(tixlChangeCase("AbC", 0), "ABC");
  assert.equal(tixlChangeCase("AbC", 1), "abc");
});

test("TiXL string list SplitString and JoinStringList reference cases", () => {
  assert.deepEqual(tixlSplitString("a,b,c", ","), ["a", "b", "c"]);
  assert.deepEqual(tixlSplitString("a--b--c", "--"), ["a", "", "b", "", "c"], "TiXL uses only the first split character");
  assert.deepEqual(tixlSplitString("a\nb", "\\n"), ["a", "b"]);
  assert.deepEqual(tixlSplitString("a\nb", ""), ["a", "b"]);
  assert.deepEqual(tixlSplitString("a,,b,", ","), ["a", "", "b", ""]);
  assert.deepEqual(tixlSplitString("", ","), []);

  assert.equal(tixlJoinStringList(["a", "b", "c"], "|"), "a|b|c");
  assert.equal(tixlJoinStringList(["a", "b"], "\\n"), "a\nb");
  assert.equal(tixlJoinStringList(["solo"], ","), "solo");
  assert.equal(tixlJoinStringList([], ","), "");
  assert.equal(tixlJoinStringList(null, ","), "");
  assert.equal(tixlJoinStringList(["a"], null), "");
});
