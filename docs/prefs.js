(function () {
  var KEY = 'minimum-times-prefs-v1';
  var controls = document.querySelectorAll('input.ctl');
  try {
    var saved = JSON.parse(localStorage.getItem(KEY) || 'null');
    if (saved && typeof saved === 'object') {
      for (var i = 0; i < controls.length; i++) {
        var id = controls[i].id;
        if (typeof saved[id] === 'boolean') controls[i].checked = saved[id];
      }
    }
  } catch (e) {}
  document.addEventListener('change', function (event) {
    if (!event.target || event.target.className !== 'ctl') return;
    try {
      var state = {};
      for (var i = 0; i < controls.length; i++) state[controls[i].id] = controls[i].checked;
      localStorage.setItem(KEY, JSON.stringify(state));
    } catch (e) {}
  });
})();
