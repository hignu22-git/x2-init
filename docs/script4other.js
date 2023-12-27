const themeSwitchers = document.querySelectorAll('.changeTheme');

themeSwitchers.forEach(switcher => {
    switcher.addEventListener('click', function() { 
        applyTheme(this.dataset.theme);
        localStorage.setItem('theme', this.dataset.theme);
    });
});

function applyTheme(themeName){
    let themeUrl = `../css/${themeName}.css`;
    document.querySelector('[title="theme"]').setAttribute('href', themeUrl);
}

let activeTheme = localStorage.getItem('theme');

if(activeTheme === null) {
    applyTheme('light');
}else {
    applyTheme(activeTheme);
}