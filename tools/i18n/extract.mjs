// Moves the words the app shows out of the code and into
// src/i18n/strings/en.json (and so into res/values/strings.xml, where
// Google Play's translation reaches them), replacing each with tx('key').
//
//   node tools/i18n/extract.mjs            report what would move (no changes)
//   node tools/i18n/extract.mjs --write    move them
//
// What counts as words the app shows:
//  - text between JSX tags, and text-like JSX props (title, label, ...);
//  - text-like object fields (title, about, description, ...) in screens and services;
//  - Alert.alert titles and messages, and set...Note/Message/Error(...) calls;
//  - template strings among those (their ${...} parts become {placeholders}).
// Never moved: identifiers ("photo", "scene-swap"), strings the code
// compares against (===, switch, includes), brand names on their own,
// test ids, styles, URLs. Code is edited in place (no reformatting).
import { readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, relative } from 'node:path';

const require = createRequire(import.meta.url);
const { parse } = require('@babel/parser');
const traverse = require('@babel/traverse').default;

const root = process.cwd();
const write = process.argv.includes('--write');
const enPath = join(root, 'src/i18n/strings/en.json');
const en = JSON.parse(readFileSync(enPath, 'utf8'));
const FN = 'tx';

// Names stay as they are in every language (the app's brand and its tools).
const BRANDS = new Set([
  'Photo Plus',
  'Premium',
  'Photo Plus Premium',
  // Apps people share to.
  'WhatsApp Business',
  'Messenger',
  'Snapchat',
  'Telegram',
  'Threads',
  'Pinterest',
  'Messages',
  'LinkedIn',
  'X',
  'Relight',
  'Glide',
  'Spotlight',
  'Lively',
  'Clarity',
  'Scene Swap',
  'Film & Collage',
  'Product Studio',
  'Photoshoot Studio',
  'Vibe Match',
  'Transform',
  'Moonlight',
  'Ultra Detail',
  'Glow',
  'Google Play',
  'WhatsApp',
  'Instagram',
  'Facebook',
  'TikTok',
  'JPEG',
  'PNG',
  'WebP',
  'HDR',
  'ISO',
  'WB',
  'RAW',
  '4K',
  'HD',
  'AI',
  'OK',
  'Pro',
  'Auto Light',
  'Lively colour',
  'Beauty & Spotlight',
  'Glide & Film',
]);

const TEXT_PROPS = new Set([
  'title',
  'subtitle',
  'label',
  'overline',
  'message',
  'placeholder',
  'accessibilityLabel',
  'accessibilityHint',
  'hint',
  'description',
  'caption',
  'text',
  'tooltip',
  'emptyText',
  'confirmLabel',
  'cancelLabel',
  'body',
  'heading',
  'detail',
  'note',
  'about',
  'tip',
  'help',
  'badge',
  'value',
  'actionLabel',
  'emptyTitle',
  'emptyBody',
  'headline',
  'summary',
  'cta',
  'explain',
  'why',
  'blurb',
  'short',
  'long',
  'lead',
  'info',
  'warning',
  'error',
  'success',
  'prompt',
  'question',
  'answer',
  'name',
  'unitLabel',
  'onLabel',
  'offLabel',
  'tagline',
  'sub',
]);
const SET_CALL =
  /^(set(Note|Message|Error|Result|Toast|Status|Hint|Tip|Notice|Info|Warning|Banner|Busy(Text|Label)?|Progress(Text|Label)?|Caption|Feedback|Problem|Reason|Explain)|toast|notify|showToast|announce|say|flash)$/;

const isTextLike = s => {
  const v = s.trim();
  if (!/\p{L}/u.test(v)) return false;
  if (BRANDS.has(v)) return false;
  if (/^https?:|^www\.|^#[0-9a-f]{3,8}$|^asset:|^file:|\.(jpg|png|json|tflite|task)$/i.test(v)) return false;
  if (/^[a-z0-9_.\-/:@$]+$/.test(v)) return false; // an identifier, key, path or name in code
  if (/^[A-Z0-9_]+$/.test(v) && v.length < 3) return false;
  if (/^[A-Z][a-zA-Z0-9]+$/.test(v) && /[a-z][A-Z]/.test(v)) return false; // CamelCase identifiers
  return true;
};

function files(dir) {
  const out = [];
  for (const f of readdirSync(dir)) {
    const p = join(dir, f);
    if (statSync(p).isDirectory()) out.push(...files(p));
    else if (/\.(ts|tsx)$/.test(f) && !/\.d\.ts$/.test(f)) out.push(p);
  }
  return out;
}
const SKIP = [
  /src\/i18n\//,
  /src\/native\/specs\//,
  /src\/services\/rewards\/captions\.ts$/,
  /src\/theme\//,
  /src\/services\/logging\//,
];
const all = files(join(root, 'src')).filter(f => !SKIP.some(r => r.test(f)));

const parseFile = src => parse(src, { sourceType: 'module', plugins: ['typescript', 'jsx'], errorRecovery: false });

// Strings the code compares against anywhere: they are values, not words.
const compared = new Set();
for (const f of all) {
  const ast = parseFile(readFileSync(f, 'utf8'));
  traverse(ast, {
    BinaryExpression(p) {
      if (!['===', '!==', '==', '!='].includes(p.node.operator)) return;
      for (const n of [p.node.left, p.node.right]) if (n.type === 'StringLiteral') compared.add(n.value);
    },
    SwitchCase(p) {
      if (p.node.test?.type === 'StringLiteral') compared.add(p.node.test.value);
    },
    CallExpression(p) {
      const c = p.node.callee;
      if (
        c.type === 'MemberExpression' &&
        ['includes', 'has', 'indexOf', 'startsWith', 'endsWith', 'get'].includes(c.property.name)
      )
        for (const a of p.node.arguments) if (a.type === 'StringLiteral') compared.add(a.value);
    },
    TSLiteralType(p) {
      if (p.node.literal.type === 'StringLiteral') compared.add(p.node.literal.value);
    },
  });
}

const slug = s =>
  s
    .normalize('NFKD')
    .replace(/[̀-ͯ]/g, '')
    .toLowerCase()
    .replace(/\{\w+\}/g, ' ')
    .replace(/[^a-z0-9]+/g, ' ')
    .trim()
    .split(' ')
    .filter(Boolean)
    .slice(0, 5)
    .join('_') || 'text';
const prefixOf = f =>
  relative(join(root, 'src'), f)
    .replace(/\.(tsx?|jsx?)$/, '')
    .split('/')
    .slice(-1)[0]
    .replace(/(Screen|Panel|Sheet|Controls|Service)$/, '')
    .replace(/([a-z])([A-Z])/g, '$1_$2')
    .toLowerCase()
    .replace(/[^a-z0-9_]/g, '_');

const byText = new Map(Object.entries(en).map(([k, v]) => [`${k.split('_')[0]}|${v}`, k]));
const added = {};
let total = 0;
const report = [];

for (const file of all) {
  const src = readFileSync(file, 'utf8');
  const ast = parseFile(src);
  const prefix = prefixOf(file);
  const edits = []; // { start, end, make(rewrite) }
  const keyFor = text => {
    const id = `${prefix}|${text}`;
    if (byText.has(id)) return byText.get(id);
    let key = `${prefix}_${slug(text)}`.replace(/_+/g, '_').slice(0, 80);
    if (!/^[a-z]/.test(key)) key = `s_${key}`;
    let k = key;
    for (let i = 2; k in en || k in added; i++) k = `${key}_${i}`;
    added[k] = text;
    byText.set(id, k);
    return k;
  };
  const paramName = (node, used) => {
    let n =
      node.type === 'Identifier'
        ? node.name
        : node.type === 'MemberExpression' && !node.computed
          ? node.property.name
          : node.type === 'CallExpression' && node.callee.type === 'Identifier'
            ? node.callee.name.replace(/^(format|to|get)/, '').replace(/^./, c => c.toLowerCase()) || 'value'
            : 'value';
    n = n.replace(/[^A-Za-z0-9]/g, '') || 'value';
    let m = n;
    for (let i = 2; used.has(m); i++) m = `${n}${i}`;
    used.add(m);
    return m;
  };
  // A string or template as a tx(...) call; nested edits inside ${...} are kept.
  const callFor = (node, rewrite) => {
    if (node.type === 'StringLiteral') return `${FN}('${keyFor(node.value)}')`;
    const used = new Set();
    let text = '';
    const params = [];
    node.quasis.forEach((q, i) => {
      text += q.value.cooked;
      const e = node.expressions[i];
      if (e) {
        const name = paramName(e, used);
        text += `{${name}}`;
        const code = rewrite(e.start, e.end);
        params.push(code === name ? name : `${name}: ${code}`);
      }
    });
    return `${FN}('${keyFor(text)}'${params.length ? `, { ${params.join(', ')} }` : ''})`;
  };
  const textOf = node =>
    node.type === 'StringLiteral'
      ? node.value
      : node.type === 'TemplateLiteral'
        ? node.quasis.map(q => q.value.cooked).join(' ')
        : null;
  const wanted = node => {
    const s = textOf(node);
    if (s == null || !isTextLike(s)) return false;
    if (node.type === 'StringLiteral' && compared.has(node.value)) return false;
    return true;
  };
  const add = (node, mode) => edits.push({ start: node.start, end: node.end, node, mode });

  // Strings inside an expression that is shown (conditionals, ||, ??, templates).
  const shownExpr = n => {
    if (!n) return;
    if (n.type === 'StringLiteral' || n.type === 'TemplateLiteral') {
      if (wanted(n)) add(n, 'expr');
      else if (n.type === 'TemplateLiteral') n.expressions.forEach(shownExpr);
    } else if (n.type === 'ConditionalExpression') {
      shownExpr(n.consequent);
      shownExpr(n.alternate);
    } else if (n.type === 'LogicalExpression') {
      shownExpr(n.left);
      shownExpr(n.right);
    } else if (n.type === 'TSAsExpression' || n.type === 'ParenthesizedExpression') shownExpr(n.expression);
  };

  traverse(ast, {
    JSXText(p) {
      const raw = p.node.value;
      const v = raw.trim();
      if (!v || !isTextLike(v.replace(/\s+/g, ' '))) return;
      const lead = raw.length - raw.trimStart().length;
      const trail = raw.length - raw.trimEnd().length;
      edits.push({
        start: p.node.start + lead,
        end: p.node.end - trail,
        node: { type: 'StringLiteral', value: v.replace(/\s+/g, ' ') },
        mode: 'jsxText',
      });
    },
    JSXAttribute(p) {
      const name = p.node.name.name;
      if (!TEXT_PROPS.has(name) || /^(testID|key|style|icon|id)$/.test(name)) return;
      const v = p.node.value;
      if (!v) return;
      if (v.type === 'StringLiteral') {
        if (name === 'name' && !/\s/.test(v.value)) return; // icon or route names
        if (wanted(v)) add(v, 'attr');
      } else if (v.type === 'JSXExpressionContainer') shownExpr(v.expression);
    },
    JSXExpressionContainer(p) {
      if (p.parent.type === 'JSXElement' || p.parent.type === 'JSXFragment') shownExpr(p.node.expression);
    },
    ObjectProperty(p) {
      const k = p.node.key;
      const name = k.type === 'Identifier' ? k.name : k.type === 'StringLiteral' ? k.value : null;
      if (!name || !TEXT_PROPS.has(name)) return;
      const v = p.node.value;
      if (name === 'name' && v.type === 'StringLiteral' && !/\s/.test(v.value) && !/^[A-Z]/.test(v.value)) return;
      // Navigation routes ({ name: 'Main' } in routes: [...]) are screens, not words.
      if (name === 'name' && p.parentPath.parentPath?.parentPath?.node?.key?.name === 'routes') return;
      if (v.type === 'StringLiteral' || v.type === 'TemplateLiteral') {
        if (wanted(v)) add(v, 'expr');
      } else shownExpr(v);
    },
    CallExpression(p) {
      const c = p.node.callee;
      const args = p.node.arguments;
      if (c.type === 'MemberExpression' && c.object.name === 'Alert' && ['alert', 'prompt'].includes(c.property.name)) {
        args.slice(0, 2).forEach(shownExpr);
      } else if (c.type === 'Identifier' && SET_CALL.test(c.name)) {
        args.forEach(a => (a.type === 'ObjectExpression' ? null : shownExpr(a)));
      }
    },
  });

  if (!edits.length) continue;
  // Drop duplicates (a node reached twice) and edits inside other edits' ranges are applied through rewrite().
  const uniq = [...new Map(edits.map(e => [`${e.start}:${e.end}`, e])).values()].sort((a, b) => a.start - b.start);
  const outer = uniq.filter(
    e => !uniq.some(o => o !== e && o.start <= e.start && o.end >= e.end && (o.start < e.start || o.end > e.end)),
  );
  const rewrite = (start, end) => {
    let out = '';
    let at = start;
    for (const e of uniq) {
      if (e.start < start || e.end > end || e.start < at) continue;
      if (
        uniq.some(
          o =>
            o !== e &&
            o.start >= start &&
            o.end <= end &&
            o.start <= e.start &&
            o.end >= e.end &&
            (o.start < e.start || o.end > e.end),
        )
      )
        continue;
      out += src.slice(at, e.start) + make(e);
      at = e.end;
    }
    return out + src.slice(at, end);
  };
  const make = e => {
    const call = callFor(e.node, rewrite);
    return e.mode === 'jsxText' || e.mode === 'attr' ? `{${call}}` : call;
  };
  let out = '';
  let at = 0;
  for (const e of outer) {
    out += src.slice(at, e.start) + make(e);
    at = e.end;
  }
  out += src.slice(at);
  // Bring in tx unless the file has it already.
  const rel = relative(dirname(file), join(root, 'src/i18n')).replace(/\\/g, '/');
  const spec = rel.startsWith('.') ? rel : `./${rel}`;
  if (!new RegExp(`\\b${FN}\\b[^\\n]*from '${spec.replace(/[.*+?^${}()|[\]\\/]/g, '\\$&')}'`).test(out)) {
    const imports = [...out.matchAll(/^import[\s\S]*?from '[^']+';\n/gm)];
    const last = imports.at(-1);
    const line = `import { t as ${FN} } from '${spec}';\n`;
    out = last ? out.slice(0, last.index + last[0].length) + line + out.slice(last.index + last[0].length) : line + out;
  }
  total += outer.length;
  report.push(`${relative(root, file)}: ${outer.length}`);
  if (write) writeFileSync(file, out);
}

console.log(report.join('\n'));
console.log(`\n${total} edits, ${Object.keys(added).length} new strings`);
if (write) {
  writeFileSync(enPath, `${JSON.stringify({ ...en, ...added }, null, 2)}\n`);
} else {
  writeFileSync(join(process.env.TMPDIR ?? '/tmp', 'i18n-extract-preview.json'), JSON.stringify(added, null, 2));
}
