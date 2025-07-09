#!/bin/bash

# Test script to verify SHM Gist badge accessibility

echo "🔍 Testing SHM Coverage Badge Gist Accessibility"
echo "================================================"

GIST_ID="0de6c8879fb6085dd4e0fdbc3b4cf451"
SHM_FILE="shm_coverage.json"
GIST_URL="https://gist.githubusercontent.com/hijimasa/${GIST_ID}/raw/${SHM_FILE}"
BADGE_URL="https://img.shields.io/endpoint?url=${GIST_URL}"

echo "🌐 Gist URL: $GIST_URL"
echo "🏷️  Badge URL: $BADGE_URL"
echo ""

# Test Gist accessibility
echo "📡 Testing Gist accessibility..."
if curl -s -f "$GIST_URL" > /dev/null; then
    echo "✅ Gist is accessible"
    echo "📄 Gist content:"
    curl -s "$GIST_URL" | jq . 2>/dev/null || curl -s "$GIST_URL"
else
    echo "❌ Gist is not accessible"
    echo "💡 Please ensure the following:"
    echo "   1. Gist exists and is public: https://gist.github.com/${GIST_ID}"
    echo "   2. File '${SHM_FILE}' exists in the Gist"
    echo "   3. File contains valid JSON for shields.io endpoint"
fi

echo ""

# Test badge rendering
echo "🏷️  Testing badge rendering..."
if curl -s -f "$BADGE_URL" > /dev/null; then
    echo "✅ Badge renders successfully"
    echo "🌐 Badge URL: $BADGE_URL"
else
    echo "❌ Badge rendering failed"
    echo "💡 Check the Gist content format"
fi

echo ""
echo "📋 Expected JSON format for shm_coverage.json:"
cat << 'EOF'
{
  "schemaVersion": 1,
  "label": "coverage",
  "message": "85.2%",
  "color": "brightgreen"
}
EOF

echo ""
echo "🔧 To manually create the file in Gist:"
echo "   1. Go to: https://gist.github.com/${GIST_ID}"
echo "   2. Click 'Edit'"
echo "   3. Add a new file named '${SHM_FILE}'"
echo "   4. Copy the JSON format above and paste it"
echo "   5. Save the Gist"
