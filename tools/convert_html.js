#!/usr/bin/env node
/**
 * Convert HTML file to C++ header file with minification.
 * Reads index.html and generates ../src/CaptivePage.h
 * Uses html-minifier for optimal compression.
 * 
 * Generates BOTH versions (gzip is default). Use `SWM_DISABLE_GZIP` to
 * select the uncompressed variant at build time.
 * 
 * Usage: node convert_html.js
 */

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const minify = require('html-minifier').minify;
let terser;

const scriptDir = __dirname;
const htmlFile = path.join(scriptDir, 'index.html');
const outputFile = path.join(scriptDir, '..', 'src', 'CaptivePage.h');

function fmtBytes(n) {
  return `${n} bytes`;
}

function fmtPct(from, to) {
  if (!from || from === 0) return '0.0%';
  return `${(100 * (1 - to / from)).toFixed(1)}%`;
}

async function minifyHtml(htmlContent) {
  // first minify inline <script> blocks with terser (if available)
  let inlineOriginal = 0;
  let inlineMinified = 0;
  try {
    if (terser) {
      const inlineResult = await minifyInlineJs(htmlContent);
      if (inlineResult && typeof inlineResult === 'object') {
        htmlContent = inlineResult.html;
        inlineOriginal = inlineResult.originalSize || 0;
        inlineMinified = inlineResult.minifiedSize || 0;
      } else {
        htmlContent = inlineResult;
      }
    }
  } catch (e) {
    console.error('Inline JS minify failed:', e);
  }
  const postInlineSize = Buffer.byteLength(htmlContent, 'utf-8');
  const minifiedHtml = minify(htmlContent, {
    removeComments: true,
    removeRedundantAttributes: true,
    removeScriptTypeAttributes: true,
    removeStyleLinkTypeAttributes: true,
    minifyCSS: true,
    // we already minify inline JS with terser above, avoid double-processing
    minifyJS: false,
    minifyURLs: false,
    collapseWhitespace: true,
    conservativeCollapse: false,
    decodeEntities: false,
  });
  return { html: minifiedHtml, inlineOriginal, inlineMinified, postInlineSize };
}

async function minifyInlineJs(html) {
  // match <script> blocks without a src attribute
  const re = /<script\b(?![^>]*\bsrc=)([^>]*)>([\s\S]*?)<\/script>/gi;
  let out = '';
  let lastIndex = 0;
  let match;
  let originalTotal = 0;
  let minifiedTotal = 0;
  while ((match = re.exec(html)) !== null) {
    const index = match.index;
    out += html.slice(lastIndex, index);
    const attrs = match[1] || '';
    const code = match[2] || '';
    const trimmed = code.trim();
    if (!trimmed) {
      out += match[0];
      lastIndex = re.lastIndex;
      continue;
    }
    originalTotal += Buffer.byteLength(trimmed, 'utf-8');
    if (!terser || typeof terser.minify !== 'function') {
      console.error('terser not available or invalid');
      out += match[0];
      minifiedTotal += Buffer.byteLength(trimmed, 'utf-8');
      lastIndex = re.lastIndex;
      continue;
    }
    try {
      const result = await terser.minify(trimmed, { compress: true, mangle: true });
      if (!result || result.error || !result.code) {
        console.error('terser returned unexpected result:', result);
        out += match[0];
        minifiedTotal += Buffer.byteLength(trimmed, 'utf-8');
      } else {
        const open = `<script${attrs}>`;
        out += open + result.code + '</script>';
        minifiedTotal += Buffer.byteLength(result.code, 'utf-8');
      }
    } catch (e) {
      console.error('terser threw exception:', e && e.message ? e.message : e);
      out += match[0];
    }
    lastIndex = re.lastIndex;
  }
  out += html.slice(lastIndex);
  return { html: out, originalSize: originalTotal, minifiedSize: minifiedTotal };
}

function generateHeader(htmlContent, gzipData) {
  const header = `#pragma once

#include <Arduino.h>

namespace HtmlPages
{
#if !defined(SWM_DISABLE_GZIP)
  // ============================================================================
  // GZIP COMPRESSED VERSION (default)
  // ============================================================================
  // Compressed size: ${gzipData.length} bytes (${(100 * (1 - gzipData.length / Buffer.byteLength(htmlContent))).toFixed(1)}% of minified)
  // Original decompressed size: ${Buffer.byteLength(htmlContent, 'utf-8')} bytes
  // The browser will decompress this automatically when receiving Content-Encoding: gzip
  
  constexpr const uint8_t captiveSiteHtmlGzip[] = {
    ${Array.from(gzipData).map(b => '0x' + b.toString(16).padStart(2, '0')).join(', ')}
  };
  constexpr size_t captiveSiteHtmlGzipSize = sizeof(captiveSiteHtmlGzip);

#else
  // ============================================================================
  // NORMAL MINIFIED VERSION (no compression)
  // ============================================================================
  // Size: ${Buffer.byteLength(htmlContent, 'utf-8')} bytes
  
  constexpr char captiveSiteHtml[] = R"rawliteral(
${htmlContent}
)rawliteral";

#endif  // SWM_DISABLE_GZIP
}

`;

  return header;
}

async function main() {
  // Check if html-minifier is installed
  try {
    require.resolve('html-minifier');
  } catch (err) {
    console.log('Installing dependencies...');
    const { execSync } = require('child_process');
    try {
      execSync('npm install', { cwd: scriptDir, stdio: 'inherit' });
    } catch (e) {
      console.error('Failed to install dependencies');
      process.exit(1);
    }
  }

  // Ensure terser is available (install if necessary)
  try {
    terser = require('terser');
  } catch (err) {
    console.log('terser not found, installing terser...');
    try {
      const { execSync } = require('child_process');
      execSync('npm install terser', { cwd: scriptDir, stdio: 'inherit' });
      terser = require('terser');
    } catch (e) {
      console.error('Failed to install or load terser; continuing without terser');
      terser = null;
    }
  }

  if (!fs.existsSync(htmlFile)) {
    console.error(`Error: ${htmlFile} not found!`);
    return 1;
  }

  console.log(`Reading ${htmlFile}...`);
  const htmlContent = fs.readFileSync(htmlFile, 'utf-8');

  const originalSize = Buffer.byteLength(htmlContent, 'utf-8');
  console.log(`Original: ${fmtBytes(originalSize)}`);

  console.log('Step 1 — Inline JS minification');
  const minifyResult = await minifyHtml(htmlContent);
  const minified = minifyResult.html;
  const inlineOriginal = minifyResult.inlineOriginal || 0;
  const inlineMinified = minifyResult.inlineMinified || 0;
  const postInlineSize = minifyResult.postInlineSize || Buffer.byteLength(htmlContent, 'utf-8');

  if (inlineOriginal > 0) {
    console.log(`  Inline JS: ${fmtBytes(inlineOriginal)} → ${fmtBytes(inlineMinified)}  (${fmtPct(inlineOriginal, inlineMinified)} saved)`);
  } else {
    console.log('  Inline JS: none');
  }

  console.log(`  After inline: ${fmtBytes(postInlineSize)}  (${fmtPct(originalSize, postInlineSize)} from original)`);

  const minifiedSize = Buffer.byteLength(minified, 'utf-8');
  console.log('Step 2 — HTML minification');
  console.log(`  HTML: ${fmtBytes(postInlineSize)} → ${fmtBytes(minifiedSize)}  (${fmtPct(postInlineSize, minifiedSize)} saved)`);

  console.log('Step 3 — Gzip compression');
  const gzipData = zlib.gzipSync(minified);
  const gzipSize = gzipData.length;
  console.log(`  Gzip: ${fmtBytes(minifiedSize)} → ${fmtBytes(gzipSize)}  (${fmtPct(minifiedSize, gzipSize)} saved from previous)`);
  console.log(`  Total reduction from original: ${fmtPct(originalSize, gzipSize)}`);

  console.log('\nGenerating C++ header...');
  const headerContent = generateHeader(minified, gzipData);

  // Create output directory if it doesn't exist
  const outputDir = path.dirname(outputFile);
  if (!fs.existsSync(outputDir)) {
    fs.mkdirSync(outputDir, { recursive: true });
  }

  console.log(`Writing to ${outputFile}...`);
  fs.writeFileSync(outputFile, headerContent, 'utf-8');

  const finalSize = Buffer.byteLength(headerContent, 'utf-8');
  console.log(`Final header size: ${finalSize} bytes`);
  console.log('Conversion complete!\n');
  console.log('Header file includes both versions (gzip is default):');
  console.log(`   - Gzip (default):   ${gzipSize} bytes (served with Content-Encoding: gzip)`);
  console.log(`   - Normal (uncompressed): ${minifiedSize} bytes (define SWM_DISABLE_GZIP to use)`);
  return 0;
}

main().then(code => process.exit(code)).catch(err => {
  console.error('Fatal error:', err);
  process.exit(2);
});
