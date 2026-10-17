const fs = require('fs');

function search(file) {
    const content = fs.readFileSync(file, 'utf8');
    const lines = content.split('\n');
    lines.forEach((line, i) => {
        if (line.includes('ToolbarDockSize')) {
            console.log(`${file}:${i + 1}: ${line}`);
        }
    });
}

search('C:/dev/sumatrapdf/src/Toolbar.cpp');
search('C:/dev/sumatrapdf/src/SumatraPDF.cpp');
search('C:/dev/sumatrapdf/src/MainWindow.cpp');
search('C:/dev/sumatrapdf/src/Toolbar.h');
