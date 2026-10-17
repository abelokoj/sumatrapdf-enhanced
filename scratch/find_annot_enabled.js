const fs = require('fs');

const content = fs.readFileSync('C:/dev/sumatrapdf/src/Toolbar.cpp', 'utf8');
const lines = content.split('\n');
lines.forEach((line, i) => {
    if (line.includes('pdfAnnotationsToolbarEnabled')) {
        console.log((i + 1) + ': ' + line);
    }
});
