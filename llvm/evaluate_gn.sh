#!/bin/bash
TESTS=$(find test/Transforms/GVN/ -name "*.ll")
TOTAL=0
PASS=0
FAIL=0
CRASH=0

for test in $TESTS; do
    TOTAL=$((TOTAL+1))
    OUTPUT=$(../build/bin/opt -load-pass-plugin=../build/lib/WitnessPasses.so -passes="gvn-witness" "$test" -disable-output 2>&1)
    if [ $? -ne 0 ]; then
        CRASH=$((CRASH+1))
    elif echo "$OUTPUT" | grep -q "VERIFICATION SUCCESSFUL"; then
        PASS=$((PASS+1))
    else
        FAIL=$((FAIL+1))
    fi
    if [ $((TOTAL % 20)) -eq 0 ]; then
        echo "Processed $TOTAL tests. $PASS passed, $FAIL failed, $CRASH crashed."
    fi
done
echo "Final: $PASS passed out of $TOTAL tests. Failed: $FAIL, Crashed: $CRASH"
