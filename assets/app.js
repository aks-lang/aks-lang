function toggleTheme() {
  const current = document.documentElement.getAttribute('data-theme') || 'dark';
  const next = current === 'dark' ? 'light' : 'dark';
  document.documentElement.setAttribute('data-theme', next);
  localStorage.setItem('aks-theme', next);

  if (window.mermaid) {
    try {
      mermaid.initialize({ startOnLoad: false, theme: next === 'dark' ? 'dark' : 'default' });
    } catch (e) {}
  }
}

function copyCode(btn) {
  const wrapper = btn.closest('.code-wrapper');
  const codeEl = wrapper ? wrapper.querySelector('pre code') : null;
  if (!codeEl) return;
  const text = codeEl.innerText;
  navigator.clipboard.writeText(text).then(() => {
    const orig = btn.innerText;
    btn.innerText = 'Copied!';
    btn.classList.add('copied');
    setTimeout(() => {
      btn.innerText = orig;
      btn.classList.remove('copied');
    }, 2000);
  });
}

function copyInstall(btn, text) {
  navigator.clipboard.writeText(text).then(() => {
    const orig = btn.innerText;
    btn.innerText = 'Copied!';
    btn.classList.add('copied');
    setTimeout(() => {
      btn.innerText = orig;
      btn.classList.remove('copied');
    }, 2000);
  });
}