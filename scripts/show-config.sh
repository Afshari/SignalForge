#!/bin/bash

CONFIG=${1:-config.json}

echo "--- SignalForge config ---"
python3 -c "
import json
c = json.load(open('$CONFIG'))
print('sha256  :', c['kernels']['sha256'])
print('fft     :', c['kernels']['fft'])
print('pipeline:', c['pipeline'])
print('redis   :', {k: c['redis'][k] for k in ['host', 'port', 'db']})
"
echo "--------------------------"