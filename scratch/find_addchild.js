const fs = require('fs');

const content = fs.readFileSync('C:/dev/sumatrapdf/src/Toolbar.cpp', 'utf8');

// Let's search for all AddChild calls on mainRow
const lines = content.split('\n');
lines.forEach((line, i) => {
    if (line.includes('mainRow->AddChild(')) {
        console.log((i + 1) + ': ' + line);
    }
});
