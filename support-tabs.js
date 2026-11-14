const links = document.querySelectorAll('.support-tabs a');
function selectSupportTab() {
  const projects = location.hash === '#projets';
  document.getElementById('soutien').hidden = projects;
  document.getElementById('projets').hidden = !projects;
  links.forEach(link => {
    if (link.hash === (projects ? '#projets' : '#soutien')) {
      link.setAttribute('aria-current', 'true');
    } else {
      link.removeAttribute('aria-current');
    }
  });
}
window.addEventListener('hashchange', selectSupportTab);
selectSupportTab();
