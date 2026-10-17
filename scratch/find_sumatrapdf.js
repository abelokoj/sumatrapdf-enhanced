const fs = require('fs');

const content = fs.readFileSync('C:/dev/sumatrapdf/src/SumatraPDF.cpp', 'utf8');
const lines = content.split('\n');
lines.forEach((line, i) => {
    if (line.includes('kUiToolbarDirty') || line.includes('ToolbarDockSize')) {
        console.log(`${i + 1}: ${line}`);
    }
});
