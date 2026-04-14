module.exports = [
  {
    "type": "heading",
    "defaultValue": "Train Timetable Settings"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Data Source"
      },
      {
        "type": "select",
        "messageKey": "KEY_URL_INDEX",
        "defaultValue": "0",
        "label": "Select Timetable URL",
        "options": [
          { "label": "URL 1", "value": "0" },
          { "label": "URL 2", "value": "1" },
          { "label": "URL 3", "value": "2" }
        ]
      },
      {
        "type": "input",
        "messageKey": "KEY_URL_0",
        "defaultValue": "",
        "label": "URL 1 (Markdown)",
        "attributes": {
          "placeholder": "https://example.com/train1.md",
          "type": "url"
        }
      },
      {
        "type": "input",
        "messageKey": "KEY_URL_1",
        "defaultValue": "",
        "label": "URL 2 (Markdown)",
        "attributes": {
          "placeholder": "https://example.com/train2.md",
          "type": "url"
        }
      },
      {
        "type": "input",
        "messageKey": "KEY_URL_2",
        "defaultValue": "",
        "label": "URL 3 (Markdown)",
        "attributes": {
          "placeholder": "https://example.com/train3.md",
          "type": "url"
        }
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Calendar Options"
      },
      {
        "type": "toggle",
        "messageKey": "KEY_HOLIDAY_CONFIG",
        "label": "Use Holiday API (Japan)",
        "defaultValue": false
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];