#pragma once

// Browser-based file manager, served at GET / by wifi_sync.cpp.
//
// Ported from CPR-vCodex's src/network/html/FilesPage.html (see that
// repo's NOTICE.md for full attribution) — but rebuilt lean rather than
// copied wholesale: the source page is a book-conversion tool (EPUB/manga
// image processing, jszip, cover thumbnails, folder tree) built for an
// e-reader's arbitrary SD library. None of that applies to a flat folder
// of small .txt notes, so this keeps only what's generic — card layout,
// CSS variables (light/dark via prefers-color-scheme), upload form, file
// table, delete confirmation — reimplemented against this device's own
// /api/files, /upload, /delete, /notes/<name> endpoints (wifi_sync.cpp).
static const char FILES_PAGE_HTML[] = R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Files - MicroWriter</title>
<style>
:root {
  --font-color: #333;
  --bg: #f5f5f5;
  --title-color: #2c3e50;
  --card-bg: #fff;
  --label-color: #7f8c8d;
  --border-color: #eee;
  --accent-color: rgb(110, 154, 130);
  --accent-color-10: rgba(110, 154, 130, 0.1);
  --accent-hover-color: #5a8c73;
  --danger-color: #c0392b;
}
@media (prefers-color-scheme: dark) {
  :root {
    --font-color: #f5f5f5;
    --bg: #333;
    --title-color: #ecf0f1;
    --card-bg: #444;
    --label-color: #bdc3c7;
    --border-color: #555;
    color-scheme: dark;
  }
}
body {
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Oxygen, Ubuntu, sans-serif;
  max-width: 700px;
  margin: 0 auto;
  padding: 20px;
  background-color: var(--bg);
  color: var(--font-color);
}
h1 {
  color: var(--title-color);
  border-bottom: 2px solid var(--accent-color);
  padding-bottom: 10px;
}
.card {
  background: var(--card-bg);
  border-radius: 8px;
  padding: 20px;
  margin: 15px 0;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
}
.dropzone {
  border: 2px dashed var(--border-color);
  border-radius: 8px;
  padding: 30px 20px;
  text-align: center;
  color: var(--label-color);
  cursor: pointer;
  transition: border-color 0.15s, background-color 0.15s;
}
.dropzone.drag-over {
  border-color: var(--accent-color);
  background: var(--accent-color-10);
}
.dropzone input { display: none; }
button, .btn {
  background: var(--accent-color);
  color: #fff;
  border: none;
  border-radius: 6px;
  padding: 8px 16px;
  font-size: 0.95em;
  cursor: pointer;
}
button:hover, .btn:hover { background: var(--accent-hover-color); }
button.danger { background: var(--danger-color); }
button:disabled { opacity: 0.5; cursor: default; }
table { width: 100%; border-collapse: collapse; }
th, td { text-align: left; padding: 10px 6px; border-bottom: 1px solid var(--border-color); }
th { color: var(--label-color); font-weight: normal; font-size: 0.85em; text-transform: uppercase; }
td.size { color: var(--label-color); white-space: nowrap; }
td.actions { text-align: right; white-space: nowrap; }
td.actions a, td.actions button { margin-left: 8px; }
.empty { color: var(--label-color); text-align: center; padding: 20px 0; }
#status { min-height: 1.2em; margin-top: 10px; font-size: 0.9em; }
#status.error { color: var(--danger-color); }
#status.ok { color: var(--accent-color); }
.modal-backdrop {
  display: none;
  position: fixed; inset: 0;
  background: rgba(0, 0, 0, 0.4);
  align-items: center; justify-content: center;
}
.modal-backdrop.open { display: flex; }
.modal { background: var(--card-bg); border-radius: 8px; padding: 20px; max-width: 320px; }
.modal-actions { display: flex; justify-content: flex-end; gap: 10px; margin-top: 16px; }
</style>
</head>
<body>
<h1>MicroWriter &mdash; Files</h1>

<div class="card">
  <div class="dropzone" id="dropzone">
    <div id="dropzoneLabel">Drop a .txt file here, or click to choose one</div>
    <input type="file" id="fileInput" accept=".txt">
  </div>
  <div id="status"></div>
</div>

<div class="card">
  <table>
    <thead><tr><th>Name</th><th>Size</th><th></th></tr></thead>
    <tbody id="fileRows"></tbody>
  </table>
  <div class="empty" id="emptyMsg" style="display:none">No notes yet.</div>
</div>

<div class="modal-backdrop" id="deleteModal">
  <div class="modal">
    <p>Delete <strong id="deleteModalName"></strong>?</p>
    <div class="modal-actions">
      <button class="btn" onclick="closeDeleteModal()" style="background:var(--label-color)">Cancel</button>
      <button class="danger" onclick="confirmDelete()">Delete</button>
    </div>
  </div>
</div>

<script>
let pendingDeleteName = null;

function escapeHtml(s) {
  return s.replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
}

function formatSize(bytes) {
  if (bytes < 1024) return bytes + ' B';
  return (bytes / 1024).toFixed(1) + ' KB';
}

function setStatus(msg, cls) {
  const el = document.getElementById('status');
  el.textContent = msg || '';
  el.className = cls || '';
}

async function loadFiles() {
  let files = [];
  try {
    const res = await fetch('/api/files');
    files = await res.json();
  } catch (e) {
    setStatus('Could not reach the device.', 'error');
    return;
  }
  const rows = document.getElementById('fileRows');
  const empty = document.getElementById('emptyMsg');
  rows.innerHTML = '';
  empty.style.display = files.length ? 'none' : 'block';
  for (const f of files) {
    const tr = document.createElement('tr');
    const safeName = escapeHtml(f.name);
    tr.innerHTML =
      '<td>' + safeName + '</td>' +
      '<td class="size">' + formatSize(f.size) + '</td>' +
      '<td class="actions">' +
        '<a class="btn" href="/notes/' + encodeURIComponent(f.name) + '" download>Download</a>' +
        '<button class="danger" onclick="openDeleteModal(\'' + f.name.replace(/'/g, "\\'") + '\')">Delete</button>' +
      '</td>';
    rows.appendChild(tr);
  }
}

function openDeleteModal(name) {
  pendingDeleteName = name;
  document.getElementById('deleteModalName').textContent = name;
  document.getElementById('deleteModal').classList.add('open');
}
function closeDeleteModal() {
  pendingDeleteName = null;
  document.getElementById('deleteModal').classList.remove('open');
}
async function confirmDelete() {
  if (!pendingDeleteName) return;
  const name = pendingDeleteName;
  closeDeleteModal();
  try {
    const res = await fetch('/delete', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'name=' + encodeURIComponent(name)
    });
    if (!res.ok) throw new Error(await res.text());
    setStatus('Deleted ' + name + '.', 'ok');
    loadFiles();
  } catch (e) {
    setStatus('Delete failed: ' + e.message, 'error');
  }
}

async function uploadFile(file) {
  if (!file.name.toLowerCase().endsWith('.txt')) {
    setStatus('Only .txt files are supported.', 'error');
    return;
  }
  setStatus('Uploading ' + file.name + '...', '');
  const form = new FormData();
  form.append('file', file);
  try {
    const res = await fetch('/upload', { method: 'POST', body: form });
    const text = await res.text();
    if (!res.ok) throw new Error(text);
    setStatus('Uploaded ' + file.name + '.', 'ok');
    loadFiles();
  } catch (e) {
    setStatus('Upload failed: ' + e.message, 'error');
  }
}

const dropzone = document.getElementById('dropzone');
const fileInput = document.getElementById('fileInput');
dropzone.addEventListener('click', () => fileInput.click());
fileInput.addEventListener('change', () => {
  if (fileInput.files.length) uploadFile(fileInput.files[0]);
  fileInput.value = '';
});
['dragenter', 'dragover'].forEach(evt =>
  dropzone.addEventListener(evt, e => { e.preventDefault(); dropzone.classList.add('drag-over'); }));
['dragleave', 'drop'].forEach(evt =>
  dropzone.addEventListener(evt, e => { e.preventDefault(); dropzone.classList.remove('drag-over'); }));
dropzone.addEventListener('drop', e => {
  const file = e.dataTransfer.files[0];
  if (file) uploadFile(file);
});

loadFiles();
</script>
</body>
</html>
)HTML";
