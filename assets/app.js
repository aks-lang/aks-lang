function copyCode(btn) {
  const pre = btn.closest('.code-wrapper').querySelector('pre code');
  if (!pre) return;
  const text = pre.innerText;
  navigator.clipboard.writeText(text).then(() => {
    const orig = btn.innerText;
    btn.innerText = 'Copied!';
    btn.style.background = '#10b981';
    setTimeout(() => {
      btn.innerText = orig;
      btn.style.background = '#374151';
    }, 2000);
  });
}

function copyInstall(btn, text) {
  navigator.clipboard.writeText(text).then(() => {
    btn.innerText = 'Copied!';
    setTimeout(() => {
      btn.innerText = 'Copy';
    }, 2000);
  });
}