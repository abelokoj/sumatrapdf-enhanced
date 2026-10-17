const fs = require('fs');
const content = fs.readFileSync('C:/dev/sumatrapdf/src/Toolbar.cpp', 'utf8');
const lines = content.split('\n');
lines.forEach((line, i) => {
    if (line.includes('ToolbarSetHeight')) {
        console.log((i + 1) + ': ' + line);
    }
});
