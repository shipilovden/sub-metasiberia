(function () {
  "use strict";

  var LANG_KEY = "msb-site-lang";
  var LANG_RU = "ru";
  var LANG_EN = "en";

  var exactMap = {
    "Log in": "\u0412\u0445\u043e\u0434",
    "log in": "\u0432\u043e\u0439\u0442\u0438",
    "Log out": "\u0412\u044b\u0439\u0442\u0438",
    "Sign up": "\u0420\u0435\u0433\u0438\u0441\u0442\u0440\u0430\u0446\u0438\u044f",
    "This is the admin site.": "\u042d\u0442\u043e \u0441\u0430\u0439\u0442 \u0430\u0434\u043c\u0438\u043d\u043a\u0430",
    "Terms": "\u0423\u0441\u043b\u043e\u0432\u0438\u044f",
    "I agree to the Terms": "\u042f \u043f\u0440\u0438\u043d\u0438\u043c\u0430\u044e \u0423\u0441\u043b\u043e\u0432\u0438\u044f",
    "You must accept Terms to sign up.": "\u0414\u043b\u044f \u0440\u0435\u0433\u0438\u0441\u0442\u0440\u0430\u0446\u0438\u0438 \u043d\u0443\u0436\u043d\u043e \u043f\u0440\u0438\u043d\u044f\u0442\u044c \u0423\u0441\u043b\u043e\u0432\u0438\u044f.",
    "This website uses cookies for login, language and basic security.": "\u042d\u0442\u043e\u0442 \u0441\u0430\u0439\u0442 \u0438\u0441\u043f\u043e\u043b\u044c\u0437\u0443\u0435\u0442 cookie \u0434\u043b\u044f \u0432\u0445\u043e\u0434\u0430, \u0432\u044b\u0431\u043e\u0440\u0430 \u044f\u0437\u044b\u043a\u0430 \u0438 \u0431\u0430\u0437\u043e\u0432\u043e\u0439 \u0431\u0435\u0437\u043e\u043f\u0430\u0441\u043d\u043e\u0441\u0442\u0438.",
    "Learn more": "\u041f\u043e\u0434\u0440\u043e\u0431\u043d\u0435\u0435",
    "Forgot password?": "\u0417\u0430\u0431\u044b\u043b\u0438 \u043f\u0430\u0440\u043e\u043b\u044c?",
    "Change password": "\u0421\u043c\u0435\u043d\u0438\u0442\u044c \u043f\u0430\u0440\u043e\u043b\u044c",
    "Admin page": "\u0410\u0434\u043c\u0438\u043d\u043a\u0430",
    "Main admin page": "\u0413\u043b\u0430\u0432\u043d\u0430\u044f \u0430\u0434\u043c\u0438\u043d\u043a\u0430",
    "Users": "\u041f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u0435\u043b\u0438",
    "Users table": "\u0422\u0430\u0431\u043b\u0438\u0446\u0430 \u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u0435\u043b\u0435\u0439",
    "ID": "ID",
    "Username": "\u041b\u043e\u0433\u0438\u043d",
    "Email": "Email",
    "Joined": "\u0417\u0430\u0440\u0435\u0433\u0438\u0441\u0442\u0440\u0438\u0440\u043e\u0432\u0430\u043d",
    "ETH address": "ETH \u0430\u0434\u0440\u0435\u0441",
    "Parcels": "\u041f\u0430\u0440\u0441\u0435\u043b\u0438",
    "Parcel Auctions": "\u0410\u0443\u043a\u0446\u0438\u043e\u043d\u044b \u043f\u0430\u0440\u0441\u0435\u043b\u0435\u0439",
    "Orders": "\u0417\u0430\u043a\u0430\u0437\u044b",
    "Eth Transactions": "ETH \u0442\u0440\u0430\u043d\u0437\u0430\u043a\u0446\u0438\u0438",
    "Map": "\u041a\u0430\u0440\u0442\u0430",
    "News Posts": "\u041d\u043e\u0432\u043e\u0441\u0442\u0438",
    "LOD Chunks": "LOD \u0447\u0430\u043d\u043a\u0438",
    "Worlds": "\u041c\u0438\u0440\u044b",
    "Admin": "\u0410\u0434\u043c\u0438\u043d\u043a\u0430",
    "ChatBots": "\u0427\u0430\u0442\u0431\u043e\u0442\u044b",
    "ChatBots:": "\u0427\u0430\u0442\u0431\u043e\u0442\u044b:",
    "Create new parcel": "\u0421\u043e\u0437\u0434\u0430\u0442\u044c \u043d\u043e\u0432\u044b\u0439 \u043f\u0430\u0440\u0441\u0435\u043b\u044c",
    "Delete parcel": "\u0423\u0434\u0430\u043b\u0438\u0442\u044c \u043f\u0430\u0440\u0441\u0435\u043b\u044c",
    "Edit parcel": "\u0420\u0435\u0434\u0430\u043a\u0442\u0438\u0440\u043e\u0432\u0430\u0442\u044c \u043f\u0430\u0440\u0441\u0435\u043b\u044c",
    "Set owner": "\u041d\u0430\u0437\u043d\u0430\u0447\u0438\u0442\u044c \u0432\u043b\u0430\u0434\u0435\u043b\u044c\u0446\u0430",
    "Manage editors": "\u0420\u0435\u0434\u0430\u043a\u0442\u043e\u0440\u044b",
    "Create auction": "\u0421\u043e\u0437\u0434\u0430\u0442\u044c \u0430\u0443\u043a\u0446\u0438\u043e\u043d",
    "Create world": "\u0421\u043e\u0437\u0434\u0430\u0442\u044c \u043c\u0438\u0440",
    "Create a new world": "\u0421\u043e\u0437\u0434\u0430\u0442\u044c \u043d\u043e\u0432\u044b\u0439 \u043c\u0438\u0440",
    "Create a new ChatBot": "\u0421\u043e\u0437\u0434\u0430\u0442\u044c \u043d\u043e\u0432\u043e\u0433\u043e \u0447\u0430\u0442\u0431\u043e\u0442\u0430",
    "Account": "\u041b\u0438\u0447\u043d\u044b\u0439 \u043a\u0430\u0431\u0438\u043d\u0435\u0442",
    "Gestures": "\u0416\u0435\u0441\u0442\u044b",
    "Developer": "\u0420\u0430\u0437\u0440\u0430\u0431\u043e\u0442\u0447\u0438\u043a",
    "Developer tools": "\u0418\u043d\u0441\u0442\u0440\u0443\u043c\u0435\u043d\u0442\u044b \u0440\u0430\u0437\u0440\u0430\u0431\u043e\u0442\u0447\u0438\u043a\u0430",
    "Ethereum": "Ethereum",
    "Reset password": "\u0421\u0431\u0440\u043e\u0441\u0438\u0442\u044c \u043f\u0430\u0440\u043e\u043b\u044c",
    "Reset password now": "\u0421\u0431\u0440\u043e\u0441\u0438\u0442\u044c \u043f\u0430\u0440\u043e\u043b\u044c \u0441\u0435\u0439\u0447\u0430\u0441",
    "Create parcel in this world": "\u0421\u043e\u0437\u0434\u0430\u0442\u044c \u043f\u0430\u0440\u0441\u0435\u043b\u044c \u0432 \u044d\u0442\u043e\u043c \u043c\u0438\u0440\u0435",
    "Owner username": "\u0412\u043b\u0430\u0434\u0435\u043b\u0435\u0446",
    "Owner username:": "\u0412\u043b\u0430\u0434\u0435\u043b\u0435\u0446:",
    "owner username:": "\u0432\u043b\u0430\u0434\u0435\u043b\u0435\u0446:",
    "Editors usernames (comma-separated)": "\u0420\u0435\u0434\u0430\u043a\u0442\u043e\u0440\u044b (\u0438\u043c\u0435\u043d\u0430 \u0447\u0435\u0440\u0435\u0437 \u0437\u0430\u043f\u044f\u0442\u0443\u044e)",
    "Editors usernames": "\u0420\u0435\u0434\u0430\u043a\u0442\u043e\u0440\u044b",
    "Editors usernames:": "\u0420\u0435\u0434\u0430\u043a\u0442\u043e\u0440\u044b:",
    "World name:": "\u041c\u0438\u0440:",
    "world name:": "\u043c\u0438\u0440:",
    "World (name or link):": "\u041c\u0438\u0440 (\u0438\u043c\u044f \u0438\u043b\u0438 \u0441\u0441\u044b\u043b\u043a\u0430):",
    "World": "\u041c\u0438\u0440",
    "Created": "\u0421\u043e\u0437\u0434\u0430\u043d",
    "Description": "\u041e\u043f\u0438\u0441\u0430\u043d\u0438\u0435",
    "Owner": "\u0412\u043b\u0430\u0434\u0435\u043b\u0435\u0446",
    "Main world": "\u041e\u0441\u043d\u043e\u0432\u043d\u043e\u0439 \u043c\u0438\u0440",
    "[unknown]": "[\u043d\u0435\u0438\u0437\u0432\u0435\u0441\u0442\u043d\u043e]",
    "Filter": "\u0424\u0438\u043b\u044c\u0442\u0440",
    "Disable matching chatbots": "\u041e\u0442\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u043f\u043e\u0434\u0445\u043e\u0434\u044f\u0449\u0438\u0445 \u0447\u0430\u0442\u0431\u043e\u0442\u043e\u0432",
    "Enable matching chatbots": "\u0412\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u043f\u043e\u0434\u0445\u043e\u0434\u044f\u0449\u0438\u0445 \u0447\u0430\u0442\u0431\u043e\u0442\u043e\u0432",
    "Delete matching chatbots": "\u0423\u0434\u0430\u043b\u0438\u0442\u044c \u043f\u043e\u0434\u0445\u043e\u0434\u044f\u0449\u0438\u0445 \u0447\u0430\u0442\u0431\u043e\u0442\u043e\u0432",
    "Disable": "\u041e\u0442\u043a\u043b\u044e\u0447\u0438\u0442\u044c",
    "Enable": "\u0412\u043a\u043b\u044e\u0447\u0438\u0442\u044c",
    "Delete": "\u0423\u0434\u0430\u043b\u0438\u0442\u044c",
    "Show filters": "\u041f\u043e\u043a\u0430\u0437\u0430\u0442\u044c \u0444\u0438\u043b\u044c\u0442\u0440\u044b",
    "Hide filters": "\u0421\u043a\u0440\u044b\u0442\u044c \u0444\u0438\u043b\u044c\u0442\u0440\u044b",
    "Filters & actions": "\u0424\u0438\u043b\u044c\u0442\u0440\u044b \u0438 \u0434\u0435\u0439\u0441\u0442\u0432\u0438\u044f",
    "Users overview": "\u041e\u0431\u0437\u043e\u0440 \u043f\u043e \u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u0435\u043b\u044f\u043c",
    "Users total": "\u0412\u0441\u0435\u0433\u043e \u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u0435\u043b\u0435\u0439",
    "Users visible": "\u041f\u043e\u043a\u0430\u0437\u0430\u043d\u043e \u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u0435\u043b\u0435\u0439",
    "Users with ETH": "\u041f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u0435\u043b\u0438 \u0441 ETH",
    "Users without ETH": "\u0411\u0435\u0437 ETH \u0430\u0434\u0440\u0435\u0441\u0430",
    "Users with email": "\u0421 email",
    "Type username/email to filter users": "\u0424\u0438\u043b\u044c\u0442\u0440 \u043f\u043e \u043b\u043e\u0433\u0438\u043d\u0443/email",
    "ETH filter": "\u0424\u0438\u043b\u044c\u0442\u0440 ETH",
    "All users": "\u0412\u0441\u0435 \u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u0435\u043b\u0438",
    "Only with ETH": "\u0422\u043e\u043b\u044c\u043a\u043e \u0441 ETH",
    "Only without ETH": "\u0422\u043e\u043b\u044c\u043a\u043e \u0431\u0435\u0437 ETH",
    "No events created yet.": "\u0421\u043e\u0431\u044b\u0442\u0438\u0439 \u043f\u043e\u043a\u0430 \u043d\u0435\u0442.",
    "You haven't created any chatbots.": "\u0412\u044b \u043f\u043e\u043a\u0430 \u043d\u0435 \u0441\u043e\u0437\u0434\u0430\u043b\u0438 \u0447\u0430\u0442\u0431\u043e\u0442\u043e\u0432.",
    "You do not own parcels yet.": "\u0423 \u0432\u0430\u0441 \u043f\u043e\u043a\u0430 \u043d\u0435\u0442 \u043f\u0430\u0440\u0441\u0435\u043b\u0435\u0439.",
    "You don't have personal worlds yet.": "\u0423 \u0432\u0430\u0441 \u043f\u043e\u043a\u0430 \u043d\u0435\u0442 \u043b\u0438\u0447\u043d\u044b\u0445 \u043c\u0438\u0440\u043e\u0432.",
    "Linked Ethereum address:": "\u041f\u0440\u0438\u0432\u044f\u0437\u0430\u043d\u043d\u044b\u0439 Ethereum-\u0430\u0434\u0440\u0435\u0441:",
    "No address linked.": "\u0410\u0434\u0440\u0435\u0441 \u043d\u0435 \u043f\u0440\u0438\u0432\u044f\u0437\u0430\u043d.",
    "Link an Ethereum address and prove you own it by signing a message": "\u041f\u0440\u0438\u0432\u044f\u0437\u0430\u0442\u044c Ethereum-\u0430\u0434\u0440\u0435\u0441 \u0438 \u043f\u043e\u0434\u0442\u0432\u0435\u0440\u0434\u0438\u0442\u044c \u0432\u043b\u0430\u0434\u0435\u043d\u0438\u0435 \u043f\u043e\u0434\u043f\u0438\u0441\u044c\u044e",
    "Claim ownership of a parcel on substrata.info based on NFT ownership": "\u041f\u043e\u0434\u0442\u0432\u0435\u0440\u0434\u0438\u0442\u044c \u0432\u043b\u0430\u0434\u0435\u043d\u0438\u0435 \u043f\u0430\u0440\u0441\u0435\u043b\u0435\u043c \u043d\u0430 substrata.info \u043f\u043e NFT",
    "Mint as a NFT": "\u041c\u0438\u043d\u0442\u0438\u0442\u044c \u043a\u0430\u043a NFT",
    "Quick actions": "\u0411\u044b\u0441\u0442\u0440\u044b\u0435 \u0434\u0435\u0439\u0441\u0442\u0432\u0438\u044f",
    "Stats": "\u0421\u0442\u0430\u0442\u0438\u0441\u0442\u0438\u043a\u0430",
    "Parcels:": "\u041f\u0430\u0440\u0441\u0435\u043b\u0438:",
    "Worlds:": "\u041c\u0438\u0440\u044b:",
    "Events:": "\u0421\u043e\u0431\u044b\u0442\u0438\u044f:",
    "username": "\u043b\u043e\u0433\u0438\u043d",
    "password": "\u043f\u0430\u0440\u043e\u043b\u044c",
    "Welcome!": "\u0414\u043e\u0431\u0440\u043e \u043f\u043e\u0436\u0430\u043b\u043e\u0432\u0430\u0442\u044c!",
    "Superadmin parcel tools": "\u0418\u043d\u0441\u0442\u0440\u0443\u043c\u0435\u043d\u0442\u044b \u0441\u0443\u043f\u0435\u0440\u0430\u0434\u043c\u0438\u043d\u0430 \u0434\u043b\u044f \u043f\u0430\u0440\u0441\u0435\u043b\u0435\u0439",
    "Open parcels admin page": "\u041e\u0442\u043a\u0440\u044b\u0442\u044c \u0430\u0434\u043c\u0438\u043d-\u0441\u0442\u0440\u0430\u043d\u0438\u0446\u0443 \u043f\u0430\u0440\u0441\u0435\u043b\u0435\u0439",
    "Root world Parcels": "\u041f\u0430\u0440\u0441\u0435\u043b\u0438 \u043a\u043e\u0440\u043d\u0435\u0432\u043e\u0433\u043e \u043c\u0438\u0440\u0430",
    "start parcel id:": "\u043d\u0430\u0447\u0430\u043b\u044c\u043d\u044b\u0439 id \u043f\u0430\u0440\u0441\u0435\u043b\u044f:",
    "end parcel id:": "\u043a\u043e\u043d\u0435\u0447\u043d\u044b\u0439 id \u043f\u0430\u0440\u0441\u0435\u043b\u044f:",
    "Regenerate/recreate parcel screenshots": "\u041f\u0435\u0440\u0435\u0441\u043e\u0437\u0434\u0430\u0442\u044c \u0441\u043a\u0440\u0438\u043d\u0448\u043e\u0442\u044b \u043f\u0430\u0440\u0441\u0435\u043b\u0435\u0439",
    "Configure where and how the parcel should be created.": "\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u0442\u0435 \u043c\u0438\u0440, \u0432\u043b\u0430\u0434\u0435\u043b\u044c\u0446\u0430 \u0438 \u043a\u043e\u043e\u0440\u0434\u0438\u043d\u0430\u0442\u044b \u043f\u0430\u0440\u0441\u0435\u043b\u044f.",
    "Origin X:": "X \u043d\u0430\u0447\u0430\u043b\u0430:",
    "Origin Y:": "Y \u043d\u0430\u0447\u0430\u043b\u0430:",
    "Size X:": "\u0420\u0430\u0437\u043c\u0435\u0440 X:",
    "Size Y:": "\u0420\u0430\u0437\u043c\u0435\u0440 Y:",
    "Min Z:": "\u041c\u0438\u043d. Z:",
    "Max Z:": "\u041c\u0430\u043a\u0441. Z:",
    "Back to account": "\u041d\u0430\u0437\u0430\u0434 \u0432 \u043b\u0438\u0447\u043d\u044b\u0439 \u043a\u0430\u0431\u0438\u043d\u0435\u0442",
    "Show script log": "\u041f\u043e\u043a\u0430\u0437\u0430\u0442\u044c \u043b\u043e\u0433 \u0441\u043a\u0440\u0438\u043f\u0442\u043e\u0432",
    "Show secrets (for Lua scripting)": "\u041f\u043e\u043a\u0430\u0437\u0430\u0442\u044c \u0441\u0435\u043a\u0440\u0435\u0442\u044b (\u0434\u043b\u044f Lua-\u0441\u043a\u0440\u0438\u043f\u0442\u043e\u0432)",
    "Main world": "\u041e\u0441\u043d\u043e\u0432\u043d\u043e\u0439 \u043c\u0438\u0440",
    "Worlds table": "\u0422\u0430\u0431\u043b\u0438\u0446\u0430 \u043c\u0438\u0440\u043e\u0432",
    "Created (UTC)": "\u0421\u043e\u0437\u0434\u0430\u043d (UTC)",
    "Community:": "\u0421\u043e\u043e\u0431\u0449\u0435\u0441\u0442\u0432\u043e:",
    "Terms of use": "\u041f\u0440\u0430\u0432\u0438\u043b\u0430 \u0438\u0441\u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u043d\u0438\u044f",
    "Bot status": "\u0421\u0442\u0430\u0442\u0443\u0441 \u0431\u043e\u0442\u0430",
    "Joined:": "\u0414\u0430\u0442\u0430 \u0440\u0435\u0433\u0438\u0441\u0442\u0440\u0430\u0446\u0438\u0438:",
    "Email:": "Email:",
    "Avatar model URL:": "URL \u043c\u043e\u0434\u0435\u043b\u0438 \u0430\u0432\u0430\u0442\u0430\u0440\u0430:",
    "Log in to Metasiberia": "\u0412\u0445\u043e\u0434 \u0432 Metasiberia",
    "User Account": "\u041b\u0438\u0447\u043d\u044b\u0439 \u043a\u0430\u0431\u0438\u043d\u0435\u0442",
    "Avatar placeholder": "\u0410\u0432\u0430\u0442\u0430\u0440 (\u0437\u0430\u0433\u043b\u0443\u0448\u043a\u0430)",
    "Username that will own the new root-world parcel": "\u041b\u043e\u0433\u0438\u043d \u0432\u043b\u0430\u0434\u0435\u043b\u044c\u0446\u0430 \u043d\u043e\u0432\u043e\u0433\u043e \u043f\u0430\u0440\u0441\u0435\u043b\u044f",
    "Comma-separated usernames with edit rights": "\u041b\u043e\u0433\u0438\u043d\u044b \u0440\u0435\u0434\u0430\u043a\u0442\u043e\u0440\u043e\u0432 \u0447\u0435\u0440\u0435\u0437 \u0437\u0430\u043f\u044f\u0442\u0443\u044e",
    "World name, /world/... URL, or sub://... link": "\u0418\u043c\u044f \u043c\u0438\u0440\u0430, URL /world/... \u0438\u043b\u0438 \u0441\u0441\u044b\u043b\u043a\u0430 sub://...",
    "Create a parcel using configured world and coordinates": "\u0421\u043e\u0437\u0434\u0430\u0442\u044c \u043f\u0430\u0440\u0441\u0435\u043b\u044c \u0441 \u0437\u0430\u0434\u0430\u043d\u043d\u044b\u043c\u0438 \u043f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u0430\u043c\u0438"
    ,
    "Visit in web browser": "\u041e\u0442\u043a\u0440\u044b\u0442\u044c \u0432 \u0431\u0440\u0430\u0443\u0437\u0435\u0440\u0435",
    "Visit in Substrata:": "\u041e\u0442\u043a\u0440\u044b\u0442\u044c \u0432 Substrata:",
    "(Click or enter URL into location bar in Substrata client)": "(\u041d\u0430\u0436\u043c\u0438\u0442\u0435 \u0438\u043b\u0438 \u0432\u0441\u0442\u0430\u0432\u044c\u0442\u0435 URL \u0432 \u0441\u0442\u0440\u043e\u043a\u0443 \u0430\u0434\u0440\u0435\u0441\u0430 \u043a\u043b\u0438\u0435\u043d\u0442\u0430 Substrata)",
    "Owner:": "\u0412\u043b\u0430\u0434\u0435\u043b\u0435\u0446:",
    "Writers:": "\u0420\u0435\u0434\u0430\u043a\u0442\u043e\u0440\u044b:",
    "[Remove]": "[\u0423\u0434\u0430\u043b\u0438\u0442\u044c]",
    "Add writer": "\u0414\u043e\u0431\u0430\u0432\u0438\u0442\u044c \u0440\u0435\u0434\u0430\u043a\u0442\u043e\u0440\u0430",
    "Edit description": "\u0418\u0437\u043c\u0435\u043d\u0438\u0442\u044c \u043e\u043f\u0438\u0441\u0430\u043d\u0438\u0435",
    "Edit title": "\u0418\u0437\u043c\u0435\u043d\u0438\u0442\u044c \u043d\u0430\u0437\u0432\u0430\u043d\u0438\u0435",
    "Dimensions:": "\u0420\u0430\u0437\u043c\u0435\u0440\u044b:",
    "Location:": "\u041b\u043e\u043a\u0430\u0446\u0438\u044f:",
    "NFT status": "NFT \u0441\u0442\u0430\u0442\u0443\u0441",
    "This parcel has not been minted as an NFT.": "\u042d\u0442\u043e\u0442 \u043f\u0430\u0440\u0441\u0435\u043b\u044c \u0435\u0449\u0435 \u043d\u0435 \u0437\u0430\u043c\u0438\u043d\u0447\u0435\u043d \u043a\u0430\u043a NFT.",
    "This parcel is being minted as an Ethereum NFT...": "\u042d\u0442\u043e\u0442 \u043f\u0430\u0440\u0441\u0435\u043b\u044c \u0441\u0435\u0439\u0447\u0430\u0441 \u043c\u0438\u043d\u0442\u0438\u0442\u0441\u044f \u043a\u0430\u043a Ethereum NFT...",
    "This parcel has been minted as an Ethereum NFT.": "\u042d\u0442\u043e\u0442 \u043f\u0430\u0440\u0441\u0435\u043b\u044c \u0443\u0436\u0435 \u0437\u0430\u043c\u0438\u043d\u0447\u0435\u043d \u043a\u0430\u043a Ethereum NFT.",
    "This parcel has been minted as an Ethereum NFT.  (Could not find minting transaction hash)": "\u042d\u0442\u043e\u0442 \u043f\u0430\u0440\u0441\u0435\u043b\u044c \u0443\u0436\u0435 \u0437\u0430\u043c\u0438\u043d\u0447\u0435\u043d \u043a\u0430\u043a Ethereum NFT. (\u0425\u0435\u0448 \u0442\u0440\u0430\u043d\u0437\u0430\u043a\u0446\u0438\u0438 \u043c\u0438\u043d\u0442\u0430 \u043d\u0435 \u043d\u0430\u0439\u0434\u0435\u043d)",
    "View minting transaction on Etherscan": "\u041f\u043e\u043a\u0430\u0437\u0430\u0442\u044c \u0442\u0440\u0430\u043d\u0437\u0430\u043a\u0446\u0438\u044e \u043c\u0438\u043d\u0442\u0430 \u043d\u0430 Etherscan",
    "View on OpenSea": "\u041e\u0442\u043a\u0440\u044b\u0442\u044c \u043d\u0430 OpenSea",
    "Current auction": "\u0422\u0435\u043a\u0443\u0449\u0438\u0439 \u0430\u0443\u043a\u0446\u0438\u043e\u043d",
    "This parcel is not up for auction on this site.": "\u042d\u0442\u043e\u0442 \u043f\u0430\u0440\u0441\u0435\u043b\u044c \u0441\u0435\u0439\u0447\u0430\u0441 \u043d\u0435 \u0432\u044b\u0441\u0442\u0430\u0432\u043b\u0435\u043d \u043d\u0430 \u0430\u0443\u043a\u0446\u0438\u043e\u043d \u043d\u0430 \u044d\u0442\u043e\u043c \u0441\u0430\u0439\u0442\u0435.",
    "Parcel owner tools": "\u0418\u043d\u0441\u0442\u0440\u0443\u043c\u0435\u043d\u0442\u044b \u0432\u043b\u0430\u0434\u0435\u043b\u044c\u0446\u0430 \u043f\u0430\u0440\u0441\u0435\u043b\u044f",
    "Regenerate screenshots": "\u041f\u0435\u0440\u0435\u0441\u043e\u0437\u0434\u0430\u0442\u044c \u0441\u043a\u0440\u0438\u043d\u0448\u043e\u0442\u044b",
    "Admin tools": "\u0410\u0434\u043c\u0438\u043d-\u0438\u043d\u0441\u0442\u0440\u0443\u043c\u0435\u043d\u0442\u044b",
    "Set parcel owner": "\u041d\u0430\u0437\u043d\u0430\u0447\u0438\u0442\u044c \u0432\u043b\u0430\u0434\u0435\u043b\u044c\u0446\u0430 \u043f\u0430\u0440\u0441\u0435\u043b\u044f",
    "Mark parcel as NFT-minted": "\u041e\u0442\u043c\u0435\u0442\u0438\u0442\u044c \u043f\u0430\u0440\u0441\u0435\u043b\u044c \u043a\u0430\u043a NFT-\u0437\u0430\u043c\u0438\u043d\u0447\u0435\u043d\u043d\u044b\u0439",
    "Mark parcel as not an NFT": "\u041e\u0442\u043c\u0435\u0442\u0438\u0442\u044c \u043f\u0430\u0440\u0441\u0435\u043b\u044c \u043a\u0430\u043a \u043d\u0435 NFT",
    "Retry parcel minting": "\u041f\u043e\u0432\u0442\u043e\u0440\u0438\u0442\u044c \u043c\u0438\u043d\u0442 \u043f\u0430\u0440\u0441\u0435\u043b\u044f",
    "Set parcel z-bounds": "\u0423\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u044c Z-\u0433\u0440\u0430\u043d\u0438\u0446\u044b \u043f\u0430\u0440\u0441\u0435\u043b\u044f",
    "Set parcel widths": "\u0423\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u044c \u0448\u0438\u0440\u0438\u043d\u0443/\u0434\u043b\u0438\u043d\u0443 \u043f\u0430\u0440\u0441\u0435\u043b\u044f",
    "(Screenshot generation may take several minutes)": "(\u0421\u043e\u0437\u0434\u0430\u043d\u0438\u0435 \u0441\u043a\u0440\u0438\u043d\u0448\u043e\u0442\u043e\u0432 \u043c\u043e\u0436\u0435\u0442 \u0437\u0430\u043d\u044f\u0442\u044c \u043d\u0435\u0441\u043a\u043e\u043b\u044c\u043a\u043e \u043c\u0438\u043d\u0443\u0442)",
    "(Sets vertices 2, 3, and 4)": "(\u041f\u0435\u0440\u0435\u0441\u0442\u0440\u0430\u0438\u0432\u0430\u0435\u0442 \u0432\u0435\u0440\u0448\u0438\u043d\u044b 2, 3 \u0438 4)",
    "Edit parcel description": "\u0418\u0437\u043c\u0435\u043d\u0438\u0442\u044c \u043e\u043f\u0438\u0441\u0430\u043d\u0438\u0435 \u043f\u0430\u0440\u0441\u0435\u043b\u044f",
    "Update description": "\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u043e\u043f\u0438\u0441\u0430\u043d\u0438\u0435",
    "Edit parcel title": "\u0418\u0437\u043c\u0435\u043d\u0438\u0442\u044c \u043d\u0430\u0437\u0432\u0430\u043d\u0438\u0435 \u043f\u0430\u0440\u0441\u0435\u043b\u044f",
    "Update title": "\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u043d\u0430\u0437\u0432\u0430\u043d\u0438\u0435",
    "Are you sure you want to mark this parcel as NFT-minted?": "\u0412\u044b \u0442\u043e\u0447\u043d\u043e \u0445\u043e\u0442\u0438\u0442\u0435 \u043e\u0442\u043c\u0435\u0442\u0438\u0442\u044c \u044d\u0442\u043e\u0442 \u043f\u0430\u0440\u0441\u0435\u043b\u044c \u043a\u0430\u043a NFT-\u0437\u0430\u043c\u0438\u043d\u0447\u0435\u043d\u043d\u044b\u0439?",
    "Are you sure you want to mark this parcel as not an NFT?": "\u0412\u044b \u0442\u043e\u0447\u043d\u043e \u0445\u043e\u0442\u0438\u0442\u0435 \u043e\u0442\u043c\u0435\u0442\u0438\u0442\u044c \u044d\u0442\u043e\u0442 \u043f\u0430\u0440\u0441\u0435\u043b\u044c \u043a\u0430\u043a \u043d\u0435 NFT?",
    "Are you sure you want to retry minting?": "\u0412\u044b \u0442\u043e\u0447\u043d\u043e \u0445\u043e\u0442\u0438\u0442\u0435 \u043f\u043e\u0432\u0442\u043e\u0440\u0438\u0442\u044c \u043c\u0438\u043d\u0442?"
  };

  var fragmentMap = [
    ["start parcel id:", "\u043d\u0430\u0447\u0430\u043b\u044c\u043d\u044b\u0439 id \u043f\u0430\u0440\u0441\u0435\u043b\u044f:"],
    ["end parcel id:", "\u043a\u043e\u043d\u0435\u0447\u043d\u044b\u0439 id \u043f\u0430\u0440\u0441\u0435\u043b\u044f:"],
    ["Created: ", "\u0421\u043e\u0437\u0434\u0430\u043d: "],
    ["description:", "\u043e\u043f\u0438\u0441\u0430\u043d\u0438\u0435:"],
    ["Total chatbots: ", "\u0412\u0441\u0435\u0433\u043e \u0447\u0430\u0442\u0431\u043e\u0442\u043e\u0432: "],
    [", shown by filter: ", ", \u043f\u043e \u0444\u0438\u043b\u044c\u0442\u0440\u0443: "],
    ["ChatBot ", "\u0427\u0430\u0442\u0431\u043e\u0442 "],
    [" | name: ", " | \u0438\u043c\u044f: "],
    [" | world: ", " | \u043c\u0438\u0440: "],
    [" | owner: ", " | \u0432\u043b\u0430\u0434\u0435\u043b\u0435\u0446: "],
    [" | state: ", " | \u0441\u043e\u0441\u0442\u043e\u044f\u043d\u0438\u0435: "],
    ["enabled", "\u0432\u043a\u043b\u044e\u0447\u0435\u043d\u043e"],
    ["disabled", "\u043e\u0442\u043a\u043b\u044e\u0447\u0435\u043d\u043e"],
    ["Delete chatbot ", "\u0423\u0434\u0430\u043b\u0438\u0442\u044c \u0447\u0430\u0442\u0431\u043e\u0442\u0430 "],
    ["Disable all matching chatbots?", "\u041e\u0442\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u0432\u0441\u0435\u0445 \u043f\u043e\u0434\u0445\u043e\u0434\u044f\u0449\u0438\u0445 \u0447\u0430\u0442\u0431\u043e\u0442\u043e\u0432?"],
    ["Enable all matching chatbots?", "\u0412\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u0432\u0441\u0435\u0445 \u043f\u043e\u0434\u0445\u043e\u0434\u044f\u0449\u0438\u0445 \u0447\u0430\u0442\u0431\u043e\u0442\u043e\u0432?"],
    ["Delete all matching chatbots?", "\u0423\u0434\u0430\u043b\u0438\u0442\u044c \u0432\u0441\u0435\u0445 \u043f\u043e\u0434\u0445\u043e\u0434\u044f\u0449\u0438\u0445 \u0447\u0430\u0442\u0431\u043e\u0442\u043e\u0432?"],
    ["Change chatbot state?", "\u0418\u0437\u043c\u0435\u043d\u0438\u0442\u044c \u0441\u043e\u0441\u0442\u043e\u044f\u043d\u0438\u0435 \u0447\u0430\u0442\u0431\u043e\u0442\u0430?"],
    ["Linked Ethereum address: ", "\u041f\u0440\u0438\u0432\u044f\u0437\u0430\u043d\u043d\u044b\u0439 Ethereum-\u0430\u0434\u0440\u0435\u0441: "],
    ["Parcel for sale, auction ends ", "\u041f\u0430\u0440\u0441\u0435\u043b\u044c \u043d\u0430 \u043f\u0440\u043e\u0434\u0430\u0436\u0435, \u0430\u0443\u043a\u0446\u0438\u043e\u043d \u0437\u0430\u043a\u043e\u043d\u0447\u0438\u0442\u0441\u044f "],
    ["Screenshot processing...", "\u0421\u043a\u0440\u0438\u043d\u0448\u043e\u0442 \u043e\u0431\u0440\u0430\u0431\u0430\u0442\u044b\u0432\u0430\u0435\u0442\u0441\u044f..."],
    [" m from the origin), ", " \u043c \u043e\u0442 \u0446\u0435\u043d\u0442\u0440\u0430), "],
    [" m above ground level", " \u043c \u043d\u0430\u0434 \u0443\u0440\u043e\u0432\u043d\u0435\u043c \u0437\u0435\u043c\u043b\u0438"],
    ["This is a small parcel in the market region. (e.g. a market stall)", "\u042d\u0442\u043e \u043c\u0430\u043b\u0435\u043d\u044c\u043a\u0438\u0439 \u043f\u0430\u0440\u0441\u0435\u043b\u044c \u0432 \u0440\u044b\u043d\u043e\u0447\u043d\u043e\u043c \u0440\u0430\u0439\u043e\u043d\u0435 (\u043d\u0430\u043f\u0440\u0438\u043c\u0435\u0440, \u0442\u043e\u0440\u0433\u043e\u0432\u0430\u044f \u0442\u043e\u0447\u043a\u0430)"],
    ["This is single level in a tower.", "\u042d\u0442\u043e \u043e\u0434\u0438\u043d \u0443\u0440\u043e\u0432\u0435\u043d\u044c \u0432 \u0431\u0430\u0448\u043d\u0435."],
    ["Set parcel vertex ", "\u0417\u0430\u0434\u0430\u0442\u044c \u0432\u0435\u0440\u0448\u0438\u043d\u0443 \u043f\u0430\u0440\u0441\u0435\u043b\u044f "],
    ["Parcel ", "\u041f\u0430\u0440\u0441\u0435\u043b\u044c "],
    ["joined ", "\u0437\u0430\u0440\u0435\u0433\u0438\u0441\u0442\u0440\u0438\u0440\u043e\u0432\u0430\u043d "],
    [" years ago", " \u043b\u0435\u0442 \u043d\u0430\u0437\u0430\u0434"],
    [" year ago", " \u0433\u043e\u0434 \u043d\u0430\u0437\u0430\u0434"],
    [" months ago", " \u043c\u0435\u0441. \u043d\u0430\u0437\u0430\u0434"],
    [" month ago", " \u043c\u0435\u0441. \u043d\u0430\u0437\u0430\u0434"],
    [" days ago", " \u0434\u043d. \u043d\u0430\u0437\u0430\u0434"],
    [" day ago", " \u0434\u0435\u043d\u044c \u043d\u0430\u0437\u0430\u0434"],
    [" hours ago", " \u0447. \u043d\u0430\u0437\u0430\u0434"],
    [" hour ago", " \u0447. \u043d\u0430\u0437\u0430\u0434"],
    [" mins ago", " \u043c\u0438\u043d. \u043d\u0430\u0437\u0430\u0434"],
    [" min ago", " \u043c\u0438\u043d. \u043d\u0430\u0437\u0430\u0434"]
  ];

  var textNodeOriginal = new WeakMap();

  function getLang() {
    try {
      var stored = window.localStorage.getItem(LANG_KEY);
      return stored === LANG_RU ? LANG_RU : LANG_EN;
    } catch (e) {
      return LANG_EN;
    }
  }

  function setLang(lang) {
    try {
      window.localStorage.setItem(LANG_KEY, lang);
    } catch (e) {
      // ignore
    }
  }

  function translateCoreText(text, lang) {
    if (lang !== LANG_RU) return text;
    if (Object.prototype.hasOwnProperty.call(exactMap, text)) return exactMap[text];

    var out = text;
    for (var i = 0; i < fragmentMap.length; i++) {
      var source = fragmentMap[i][0];
      var target = fragmentMap[i][1];
      if (out.indexOf(source) !== -1) out = out.split(source).join(target);
    }
    return out;
  }

  function translateWithSpacing(text, lang) {
    if (typeof text !== "string" || text.length === 0) return text;
    if (lang !== LANG_RU) return text;

    var leading = text.match(/^\s*/);
    var trailing = text.match(/\s*$/);
    var left = leading ? leading[0] : "";
    var right = trailing ? trailing[0] : "";
    var core = text.substring(left.length, text.length - right.length);
    if (!core) return text;

    return left + translateCoreText(core, lang) + right;
  }

  function shouldSkipElement(el) {
    var node = el;
    while (node) {
      if (node.nodeType !== 1) {
        node = node.parentElement;
        continue;
      }

      var tag = node.tagName ? node.tagName.toUpperCase() : "";
      if (tag === "SCRIPT" || tag === "STYLE" || tag === "NOSCRIPT" || tag === "TEXTAREA" || tag === "CODE" || tag === "PRE") {
        return true;
      }

      if (node.getAttribute && node.getAttribute("data-no-translate") === "1") {
        return true;
      }

      node = node.parentElement;
    }
    return false;
  }

  function translateTextNodes(lang) {
    if (!document.body) return;

    var walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT, null);
    var node;
    while ((node = walker.nextNode())) {
      var parent = node.parentElement;
      if (!parent || shouldSkipElement(parent)) continue;

      if (!textNodeOriginal.has(node)) textNodeOriginal.set(node, node.nodeValue);
      var original = textNodeOriginal.get(node);
      node.nodeValue = (lang === LANG_RU) ? translateWithSpacing(original, LANG_RU) : original;
    }
  }

  function translateInputValues(lang) {
    var controls = document.querySelectorAll("input[type='submit'], input[type='button']");
    for (var i = 0; i < controls.length; i++) {
      var el = controls[i];
      if (shouldSkipElement(el)) continue;
      if (!el.dataset.msbOriginalValue) el.dataset.msbOriginalValue = el.value || "";
      el.value = (lang === LANG_RU) ? translateCoreText(el.dataset.msbOriginalValue, LANG_RU) : el.dataset.msbOriginalValue;
    }
  }

  function translateAttributes(lang) {
    var nodes = document.querySelectorAll("[title], [aria-label], input[placeholder], textarea[placeholder], [onclick]");
    for (var i = 0; i < nodes.length; i++) {
      var el = nodes[i];
      if (shouldSkipElement(el)) continue;

      if (el.hasAttribute("title")) {
        if (!el.dataset.msbOriginalTitle) el.dataset.msbOriginalTitle = el.getAttribute("title") || "";
        el.setAttribute("title", (lang === LANG_RU) ? translateCoreText(el.dataset.msbOriginalTitle, LANG_RU) : el.dataset.msbOriginalTitle);
      }

      if (el.hasAttribute("aria-label")) {
        if (!el.dataset.msbOriginalAriaLabel) el.dataset.msbOriginalAriaLabel = el.getAttribute("aria-label") || "";
        el.setAttribute("aria-label", (lang === LANG_RU) ? translateCoreText(el.dataset.msbOriginalAriaLabel, LANG_RU) : el.dataset.msbOriginalAriaLabel);
      }

      if (el.hasAttribute("placeholder")) {
        if (!el.dataset.msbOriginalPlaceholder) el.dataset.msbOriginalPlaceholder = el.getAttribute("placeholder") || "";
        el.setAttribute("placeholder", (lang === LANG_RU) ? translateCoreText(el.dataset.msbOriginalPlaceholder, LANG_RU) : el.dataset.msbOriginalPlaceholder);
      }

      if (el.hasAttribute("onclick")) {
        if (!el.dataset.msbOriginalOnclick) el.dataset.msbOriginalOnclick = el.getAttribute("onclick") || "";
        el.setAttribute("onclick", (lang === LANG_RU) ? translateCoreText(el.dataset.msbOriginalOnclick, LANG_RU) : el.dataset.msbOriginalOnclick);
      }
    }
  }

  function applyLanguage(lang) {
    document.documentElement.setAttribute("lang", lang === LANG_RU ? LANG_RU : LANG_EN);

    translateTextNodes(lang);
    translateInputValues(lang);
    translateAttributes(lang);

    if (!document.body.dataset.msbOriginalTitle) document.body.dataset.msbOriginalTitle = document.title;
    document.title = (lang === LANG_RU) ? translateCoreText(document.body.dataset.msbOriginalTitle, LANG_RU) : document.body.dataset.msbOriginalTitle;

    var toggle = document.getElementById("msb-lang-toggle");
    if (toggle) {
      toggle.textContent = lang === LANG_RU ? "EN" : "RU";
      toggle.title = (lang === LANG_RU)
        ? "Switch interface language to English"
        : "\u041f\u0435\u0440\u0435\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u044f\u0437\u044b\u043a \u0438\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441\u0430 \u043d\u0430 \u0440\u0443\u0441\u0441\u043a\u0438\u0439";
    }
  }

  function ensureTooltips(lang) {
    var clickable = document.querySelectorAll("input[type='submit'], input[type='button'], button, a[href]");
    for (var i = 0; i < clickable.length; i++) {
      var el = clickable[i];
      if (shouldSkipElement(el)) continue;

      if (!el.hasAttribute("title")) {
        var label = "";
        if (el.tagName === "INPUT") label = (el.value || "").trim();
        else label = (el.textContent || "").trim();

        if (!label) continue;
        el.dataset.msbOriginalTitle = label;
        el.setAttribute("title", (lang === LANG_RU) ? translateCoreText(label, LANG_RU) : label);
      } else {
        if (!el.dataset.msbOriginalTitle) el.dataset.msbOriginalTitle = el.getAttribute("title") || "";
        el.setAttribute("title", (lang === LANG_RU) ? translateCoreText(el.dataset.msbOriginalTitle, LANG_RU) : el.dataset.msbOriginalTitle);
      }
    }
  }

  function ensureLangToggleButton() {
    if (document.getElementById("msb-lang-toggle")) return;

    var button = document.createElement("button");
    button.type = "button";
    button.id = "msb-lang-toggle";
    button.className = "msb-lang-toggle-button";
    button.setAttribute("data-no-translate", "1");

    button.addEventListener("click", function () {
      var nextLang = getLang() === LANG_RU ? LANG_EN : LANG_RU;
      setLang(nextLang);
      applyLanguage(nextLang);
      ensureTooltips(nextLang);
    });

    document.body.appendChild(button);
  }

  function initAccordions() {
    var toggles = document.querySelectorAll(".msb-accordion-toggle");
    for (var i = 0; i < toggles.length; i++) {
      var toggle = toggles[i];
      if (toggle.dataset.msbAccordionInit === "1") continue;
      toggle.dataset.msbAccordionInit = "1";

      toggle.addEventListener("click", function () {
        var targetId = this.getAttribute("data-accordion-target");
        if (!targetId) return;
        var panel = document.getElementById(targetId);
        if (!panel) return;
        panel.classList.toggle("is-open");
      });
    }
  }

  function initSliders() {
    var sliders = document.querySelectorAll("input[type='range']");
    for (var i = 0; i < sliders.length; i++) {
      var slider = sliders[i];
      slider.classList.add("msb-slider");

      var outputId = slider.getAttribute("data-slider-output");
      if (!outputId) continue;
      var output = document.getElementById(outputId);
      if (!output) continue;

      var updateOutput = function () {
        output.textContent = slider.value;
      };

      slider.addEventListener("input", updateOutput);
      updateOutput();
    }
  }

  function applyGlobalThemeClass() {
    if (!document.body) return;
    document.body.classList.add("msb-pane-theme");
  }

  function currentPath() {
    if (!window.location || typeof window.location.pathname !== "string") return "";
    return window.location.pathname;
  }

  function isAdminPath(path) {
    return path === "/admin" || path.indexOf("/admin") === 0;
  }

  function isControlPath(path) {
    if (path === "/account" || path.indexOf("/account") === 0) return true;

    var prefixes = [
      "/new_chatbot",
      "/edit_chatbot",
      "/create_world",
      "/change_password",
      "/script_log",
      "/secrets",
      "/prove_eth_address_owner",
      "/prove_parcel_owner_by_nft"
    ];

    for (var i = 0; i < prefixes.length; i++) {
      if (path === prefixes[i] || path.indexOf(prefixes[i] + "/") === 0) return true;
    }

    return false;
  }

  function applyPageClasses() {
    if (!document.body) return;

    var path = currentPath();

    if (isAdminPath(path)) {
      document.body.classList.add("msb-admin");
    }

    if (isControlPath(path)) {
      document.body.classList.add("msb-control");
    }
  }

  function wrapTable(table) {
    if (!table || !table.parentElement) return;
    if (!table.classList.contains("msb-table")) table.classList.add("msb-table");

    if (table.parentElement.classList.contains("msb-table-wrap")) return;

    var wrapper = document.createElement("div");
    wrapper.className = "msb-table-wrap";
    table.parentElement.insertBefore(wrapper, table);
    wrapper.appendChild(table);
  }

  function ensureAdminNavClass() {
    var nav = document.querySelector("p.msb-admin-nav");
    if (nav) return;

    var paragraphs = document.querySelectorAll("p");
    for (var i = 0; i < paragraphs.length; i++) {
      var p = paragraphs[i];
      var links = p.querySelectorAll("a[href]");
      if (!links || links.length < 4) continue;

      var hasAdmin = false;
      var hasUsers = false;
      for (var j = 0; j < links.length; j++) {
        var href = links[j].getAttribute("href") || "";
        if (href === "/admin") hasAdmin = true;
        if (href === "/admin_users") hasUsers = true;
      }

      if (hasAdmin && hasUsers) {
        p.classList.add("msb-admin-nav");
        return;
      }
    }
  }

  function enhanceAdminLayout() {
    if (!document.body || !document.body.classList.contains("msb-admin")) return;
    ensureAdminNavClass();

    var mainH2 = document.querySelectorAll("h2");
    for (var i = 0; i < mainH2.length; i++) {
      var h2 = mainH2[i];
      if (!h2.parentElement || h2.parentElement.classList.contains("msb-admin-section")) continue;

      var section = document.createElement("section");
      section.className = "grouped-region msb-admin-section";
      h2.parentElement.insertBefore(section, h2);

      var node = h2;
      while (node) {
        var next = node.nextSibling;
        if (node.nodeType === 1 && node.tagName && node.tagName.toUpperCase() === "H2" && node !== h2) break;
        section.appendChild(node);
        node = next;
      }
    }

    var tables = document.querySelectorAll("table");
    for (var t = 0; t < tables.length; t++) wrapTable(tables[t]);

    var forms = document.querySelectorAll("form");
    for (var f = 0; f < forms.length; f++) {
      var form = forms[f];
      var styleAttr = (form.getAttribute("style") || "").toLowerCase();
      if (styleAttr.indexOf("display:inline") !== -1 || styleAttr.indexOf("display: inline") !== -1) continue;
      form.classList.add("msb-admin-form");
    }
  }

  function readNumberFromField(form, name, fallback) {
    if (!form) return fallback;
    var el = form.querySelector("[name='" + name + "']");
    if (!el) return fallback;
    var raw = String(el.value || "").replace(",", ".");
    var value = parseFloat(raw);
    return Number.isFinite(value) ? value : fallback;
  }

  function updateParcelLivePreview(form) {
    if (!form) return;
    var preview = form.querySelector("[data-parcel-live-preview='1']");
    if (!preview) return;

    var sizeX = Math.max(0, readNumberFromField(form, "size_x", 0));
    var sizeY = Math.max(0, readNumberFromField(form, "size_y", 0));
    var minZ = readNumberFromField(form, "min_z", 0);
    var maxZ = readNumberFromField(form, "max_z", 0);
    var height = Math.max(0, maxZ - minZ);
    var area = sizeX * sizeY;
    var volume = area * height;

    var areaEl = preview.querySelector("[data-preview-area]");
    var heightEl = preview.querySelector("[data-preview-height]");
    var volumeEl = preview.querySelector("[data-preview-volume]");

    if (areaEl) areaEl.textContent = area.toFixed(1);
    if (heightEl) heightEl.textContent = height.toFixed(1);
    if (volumeEl) volumeEl.textContent = volume.toFixed(1);
  }

  function initAdminParcelLivePreview() {
    var forms = document.querySelectorAll("form[data-admin-create-parcel-form='1'], form[action='/admin_create_parcel']");
    for (var i = 0; i < forms.length; i++) {
      var form = forms[i];
      if (form.dataset.msbParcelPreviewInit === "1") continue;
      form.dataset.msbParcelPreviewInit = "1";

      var watchedNames = ["size_x", "size_y", "min_z", "max_z"];
      for (var j = 0; j < watchedNames.length; j++) {
        var field = form.querySelector("[name='" + watchedNames[j] + "']");
        if (!field) continue;
        field.addEventListener("input", function () {
          updateParcelLivePreview(form);
        });
        field.addEventListener("change", function () {
          updateParcelLivePreview(form);
        });
      }

      updateParcelLivePreview(form);
    }
  }

  function parseParcelIdFromElement(el) {
    if (!el) return "";
    var link = el.querySelector("a[href^='/parcel/']");
    if (!link) return "";
    var href = link.getAttribute("href") || "";
    var m = href.match(/\/parcel\/(\d+)/);
    return m ? m[1] : "";
  }

  function createParcelActionLink(href, label) {
    var a = document.createElement("a");
    a.href = href;
    a.textContent = label;
    return a;
  }

  function createDeleteParcelForm(parcelId) {
    var form = document.createElement("form");
    form.action = "/admin_delete_parcel";
    form.method = "post";
    form.style.display = "inline";

    var hidden = document.createElement("input");
    hidden.type = "hidden";
    hidden.name = "parcel_id";
    hidden.value = String(parcelId);
    form.appendChild(hidden);

    var btn = document.createElement("input");
    btn.type = "submit";
    btn.value = "Delete parcel";
    btn.addEventListener("click", function (e) {
      if (!window.confirm("Delete parcel " + parcelId + "?")) e.preventDefault();
    });
    form.appendChild(btn);
    return form;
  }

  function getOrCreateRowActions(entry, parcelId) {
    var actions = entry.querySelector(".msb-row-actions");
    if (!actions) {
      actions = document.createElement("div");
      actions.className = "msb-row-actions";
      entry.appendChild(actions);
    }
    return actions;
  }

  function dedupeDeleteParcelForms(actions, parcelId) {
    if (!actions) return;
    var forms = actions.querySelectorAll("form[action='/admin_delete_parcel']");
    for (var i = 1; i < forms.length; i++) {
      if (forms[i].parentNode) forms[i].parentNode.removeChild(forms[i]);
    }
    if (!forms.length && parcelId) {
      actions.appendChild(createDeleteParcelForm(parcelId));
    }
  }

  function isParcelEntryParagraph(p) {
    if (!p || p.tagName !== "P") return false;
    if (p.classList.contains("msb-admin-nav")) return false;

    var link = p.querySelector("a[href^='/parcel/']");
    if (!link) return false;
    var txt = String(link.textContent || "").trim().toLowerCase();
    return txt.indexOf("parcel") === 0 || txt.indexOf("парсель") === 0;
  }

  function isParcelActionNode(node, parcelId) {
    if (!node || node.nodeType !== 1) return false;
    var tag = node.tagName.toUpperCase();
    if (tag === "BR") return true;
    if (tag === "FORM") {
      var action = (node.getAttribute("action") || "").toLowerCase();
      if (action === "/admin_delete_parcel") return true;
      return false;
    }
    if (tag === "A") {
      var href = (node.getAttribute("href") || "").toLowerCase();
      if (!href) return false;
      if (href.indexOf("/admin_create_parcel_auction/") === 0) return true;
      if (href.indexOf("/admin_set_parcel_owner/") === 0) return true;
      if (href.indexOf("/add_parcel_writer?parcel_id=") === 0) return true;
      if (parcelId && href === "/parcel/" + parcelId) return true;
      return false;
    }
    if (tag === "SMALL" && node.querySelector("a")) return true;
    return false;
  }

  function ensureAdminParcelsLegacyLayout() {
    if (currentPath() !== "/admin_parcels") return;
    if (document.querySelector("table.msb-parcels-table")) return;

    var paragraphs = document.querySelectorAll("p");
    var entries = [];
    var forSale = 0;
    var sold = 0;
    var withAuction = 0;

    for (var i = 0; i < paragraphs.length; i++) {
      var p = paragraphs[i];
      if (!isParcelEntryParagraph(p)) continue;

      p.classList.add("msb-parcel-entry");
      var text = String(p.textContent || "");
      var lc = text.toLowerCase();
      p.setAttribute("data-row-search", lc);
      var parcelId = parseParcelIdFromElement(p);
      if (parcelId) p.setAttribute("data-parcel-id", parcelId);

      var hasAuction = lc.indexOf("auction") !== -1;
      if (hasAuction) withAuction++;
      if (lc.indexOf("for sale") !== -1) forSale++;
      if (lc.indexOf("sold") !== -1) sold++;

      var auctionState = "none";
      if (lc.indexOf("for sale") !== -1) auctionState = "for_sale";
      else if (lc.indexOf("sold") !== -1) auctionState = "sold";
      else if (hasAuction) auctionState = "history";
      p.setAttribute("data-auction-state", auctionState);

      var actions = getOrCreateRowActions(p, parcelId);

      var cursor = p.nextSibling;
      while (cursor) {
        var next = cursor.nextSibling;
        if (cursor.nodeType === 1 && isParcelEntryParagraph(cursor)) break;
        if (cursor.nodeType === 3 && String(cursor.textContent || "").trim() === "|") {
          cursor.parentNode.removeChild(cursor);
          cursor = next;
          continue;
        }
        if (isParcelActionNode(cursor, parcelId)) {
          if (cursor.tagName && cursor.tagName.toUpperCase() !== "BR") actions.appendChild(cursor);
          else cursor.parentNode.removeChild(cursor);
        }
        cursor = next;
      }

      if (parcelId) {
        if (!actions.querySelector("a[href='/admin_create_parcel_auction/" + parcelId + "']")) {
          actions.insertBefore(createParcelActionLink("/admin_create_parcel_auction/" + parcelId, "Create auction"), actions.firstChild);
        }
        if (!actions.querySelector("a[href='/parcel/" + parcelId + "']")) {
          actions.appendChild(createParcelActionLink("/parcel/" + parcelId, "Edit parcel"));
        }
        if (!actions.querySelector("a[href='/admin_set_parcel_owner/" + parcelId + "']")) {
          actions.appendChild(createParcelActionLink("/admin_set_parcel_owner/" + parcelId, "Set owner"));
        }
        if (!actions.querySelector("a[href='/add_parcel_writer?parcel_id=" + parcelId + "']")) {
          actions.appendChild(createParcelActionLink("/add_parcel_writer?parcel_id=" + parcelId, "Manage editors"));
        }
      }

      dedupeDeleteParcelForms(actions, parcelId);

      entries.push(p);
    }

    if (!entries.length) return;

    var firstEntry = entries[0];
    var firstH2 = document.querySelector("h2");

    if (!document.getElementById("admin-parcel-kpis")) {
      var kpi = document.createElement("div");
      kpi.className = "msb-kpi-grid";
      kpi.id = "admin-parcel-kpis";
      kpi.innerHTML =
        '<div class="msb-kpi"><div class="msb-kpi-label">Parcels total</div><div class="msb-kpi-value">' + entries.length + '</div></div>' +
        '<div class="msb-kpi"><div class="msb-kpi-label">Filtered</div><div class="msb-kpi-value" id="admin-parcel-filtered-count">' + entries.length + '</div></div>' +
        '<div class="msb-kpi"><div class="msb-kpi-label">For sale</div><div class="msb-kpi-value">' + forSale + '</div></div>' +
        '<div class="msb-kpi"><div class="msb-kpi-label">Sold</div><div class="msb-kpi-value">' + sold + '</div></div>' +
        '<div class="msb-kpi"><div class="msb-kpi-label">Without auction</div><div class="msb-kpi-value">' + Math.max(0, entries.length - withAuction) + '</div></div>';
      if (firstH2 && firstH2.parentNode) firstH2.parentNode.insertBefore(kpi, firstH2.nextSibling);
      else firstEntry.parentNode.insertBefore(kpi, firstEntry);
    }

    if (!document.getElementById("admin-parcel-local-filter")) {
      var filterRegion = document.createElement("section");
      filterRegion.className = "grouped-region";
      filterRegion.innerHTML =
        '<h3>Local filter</h3>' +
        '<input id="admin-parcel-local-filter" type="text" placeholder="Type to instantly filter parcel list">' +
        '<div id="admin-parcels-visible-count" class="field-description">Showing ' + entries.length + ' parcel(s) after local filter.</div>';
      firstEntry.parentNode.insertBefore(filterRegion, firstEntry);
    }

    if (!firstEntry.parentElement.classList.contains("msb-parcel-list-legacy")) {
      var listSection = document.createElement("section");
      listSection.className = "grouped-region msb-parcel-list-legacy";
      var header = document.createElement("h3");
      header.textContent = "Parcels list";
      listSection.appendChild(header);
      firstEntry.parentNode.insertBefore(listSection, firstEntry);
      var marker = firstEntry;
      while (marker) {
        var nextNode = marker.nextSibling;
        listSection.appendChild(marker);
        if (nextNode && nextNode.nodeType === 1 && isParcelEntryParagraph(nextNode)) {
          marker = nextNode;
          continue;
        }
        marker = nextNode;
        if (!marker || (marker.nodeType === 1 && marker.tagName === "HR")) break;
      }
    }
  }

  function initAdminParcelsLocalFilter() {
    var path = currentPath();
    if (path !== "/admin_parcels") return;

    var filterInput = document.getElementById("admin-parcel-local-filter");
    if (!filterInput) return;
    if (filterInput.dataset.msbInit === "1") return;
    filterInput.dataset.msbInit = "1";

    var table = document.querySelector("table.msb-parcels-table");
    var rows = table ? table.querySelectorAll("tbody tr[data-row-search]") : document.querySelectorAll(".msb-parcel-entry[data-row-search]");
    if (!rows || !rows.length) return;

    var countLabel = document.getElementById("admin-parcel-filtered-count");
    var visibleLabel = document.getElementById("admin-parcels-visible-count");

    var applyFilter = function () {
      var needle = String(filterInput.value || "").trim().toLowerCase();
      var visible = 0;

      for (var i = 0; i < rows.length; i++) {
        var row = rows[i];
        var haystack = row.getAttribute("data-row-search") || "";
        var show = !needle || haystack.indexOf(needle) !== -1;
        row.style.display = show ? "" : "none";
        if (show) visible++;
      }

      if (countLabel) countLabel.textContent = String(visible);
      if (visibleLabel) visibleLabel.textContent = "Showing " + visible + " parcel(s) after local filter.";
    };

    filterInput.addEventListener("input", applyFilter);
    applyFilter();
  }

  var tweakpanePromise = null;

  function ensureStylesheetOnce(id, href) {
    if (document.getElementById(id)) return;
    var link = document.createElement("link");
    link.id = id;
    link.rel = "stylesheet";
    link.href = href;
    document.head.appendChild(link);
  }

  function ensureScriptOnce(id, src) {
    return new Promise(function (resolve, reject) {
      var existing = document.getElementById(id);
      if (existing) {
        if (existing.dataset.loaded === "1") resolve();
        else existing.addEventListener("load", function () { resolve(); }, { once: true });
        return;
      }
      var script = document.createElement("script");
      script.id = id;
      script.src = src;
      script.defer = true;
      script.addEventListener("load", function () {
        script.dataset.loaded = "1";
        resolve();
      }, { once: true });
      script.addEventListener("error", reject, { once: true });
      document.head.appendChild(script);
    });
  }

  function ensureTweakpaneLoaded() {
    if (tweakpanePromise) return tweakpanePromise;
    tweakpanePromise = ensureScriptOnce("msb-tweakpane-js", "/files/tweakpane.min.js")
      .then(function () { return window.Tweakpane || null; })
      .catch(function () { return null; });
    return tweakpanePromise;
  }

  function postDeleteParcel(parcelId) {
    return fetch("/admin_delete_parcel", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
      credentials: "same-origin",
      body: "parcel_id=" + encodeURIComponent(String(parcelId))
    });
  }

  function initAdminParcelsTweakpane() {
    if (currentPath() !== "/admin_parcels") return;

    ensureTweakpaneLoaded().then(function (TP) {
      if (!TP || !TP.Pane) return;
      if (document.getElementById("msb-tweakpane-host")) return;

      var host = document.createElement("div");
      host.id = "msb-tweakpane-host";
      host.className = "msb-tweakpane-host msb-hidden";
      document.body.appendChild(host);

      var toggle = document.createElement("button");
      toggle.id = "msb-tweakpane-toggle";
      toggle.type = "button";
      toggle.className = "msb-tweakpane-toggle";
      toggle.textContent = "Show filters";
      toggle.setAttribute("title", "Filters & actions");
      document.body.appendChild(toggle);

      function setPaneVisibility(visible) {
        if (visible) host.classList.remove("msb-hidden");
        else host.classList.add("msb-hidden");
        if (visible) document.body.classList.add("has-tweakpane");
        else document.body.classList.remove("has-tweakpane");
        var label = visible ? "Hide filters" : "Show filters";
        toggle.textContent = (getLang() === LANG_RU) ? translateCoreText(label, LANG_RU) : label;
        var title = "Filters & actions";
        toggle.setAttribute("title", (getLang() === LANG_RU) ? translateCoreText(title, LANG_RU) : title);
      }

      toggle.addEventListener("click", function () {
        setPaneVisibility(host.classList.contains("msb-hidden"));
      });

      var pane = new TP.Pane({ title: "Filters & actions", container: host });
      var state = {
        search: "",
        only_for_sale: false,
        without_auction: false,
        compact_rows: false,
        confirm_delete: true
      };
      var stats = { total: 0, visible: 0 };

      var pages = pane.addTab({
        pages: [
          { title: "Filters" },
          { title: "Actions" },
          { title: "View" },
          { title: "Stats" }
        ]
      });

      pages.pages[0].addInput(state, "search", { label: "search" }).on("change", function () {
        var filterInput = document.getElementById("admin-parcel-local-filter");
        if (!filterInput) return;
        filterInput.value = state.search;
        filterInput.dispatchEvent(new Event("input", { bubbles: true }));
        applyTweakpaneState();
      });
      pages.pages[0].addInput(state, "only_for_sale", { label: "for sale" }).on("change", applyTweakpaneState);
      pages.pages[0].addInput(state, "without_auction", { label: "no auction" }).on("change", applyTweakpaneState);
      pages.pages[2].addInput(state, "compact_rows", { label: "compact" }).on("change", applyTweakpaneState);
      pages.pages[2].addInput(state, "confirm_delete", { label: "confirm del" });

      pages.pages[1].addButton({ title: "Reset filters" }).on("click", function () {
        state.search = "";
        state.only_for_sale = false;
        state.without_auction = false;
        var input = document.getElementById("admin-parcel-local-filter");
        if (input) {
          input.value = "";
          input.dispatchEvent(new Event("input", { bubbles: true }));
        }
        pane.refresh();
        applyTweakpaneState();
      });

      pages.pages[1].addButton({ title: "Delete visible parcels" }).on("click", function () {
        var visible = document.querySelectorAll(".msb-parcel-entry[data-visible='1']");
        if (!visible.length) return;
        if (state.confirm_delete && !window.confirm("Delete " + visible.length + " visible parcel(s)?")) return;

        var ids = [];
        for (var i = 0; i < visible.length; i++) {
          var id = visible[i].getAttribute("data-parcel-id");
          if (id) ids.push(id);
        }
        if (!ids.length) return;

        var chain = Promise.resolve();
        for (var j = 0; j < ids.length; j++) {
          (function (parcelId) {
            chain = chain.then(function () { return postDeleteParcel(parcelId); });
          })(ids[j]);
        }
        chain.then(function () { window.location.reload(); });
      });

      pages.pages[3].addMonitor(stats, "total", { label: "total", interval: 500 });
      pages.pages[3].addMonitor(stats, "visible", { label: "visible", interval: 500 });

      function applyTweakpaneState() {
        var rows = document.querySelectorAll(".msb-parcel-entry[data-row-search]");
        stats.total = rows.length;
        var visible = 0;

        for (var i = 0; i < rows.length; i++) {
          var row = rows[i];
          var rowVisible = row.style.display !== "none";
          if (rowVisible && state.only_for_sale && row.getAttribute("data-auction-state") !== "for_sale") rowVisible = false;
          if (rowVisible && state.without_auction && row.getAttribute("data-auction-state") !== "none") rowVisible = false;
          row.style.display = rowVisible ? "" : "none";
          row.setAttribute("data-visible", rowVisible ? "1" : "0");
          if (state.compact_rows) row.classList.add("msb-parcel-entry-compact");
          else row.classList.remove("msb-parcel-entry-compact");
          if (rowVisible) visible++;
        }

        stats.visible = visible;
        var filteredLabel = document.getElementById("admin-parcel-filtered-count");
        if (filteredLabel) filteredLabel.textContent = String(visible);
        var visibleLabel = document.getElementById("admin-parcels-visible-count");
        if (visibleLabel) visibleLabel.textContent = "Showing " + visible + " parcel(s) after filters.";
      }

      applyTweakpaneState();
      setPaneVisibility(false);
      document.addEventListener("input", function (e) {
        if (e.target && e.target.id === "admin-parcel-local-filter") applyTweakpaneState();
      });
    });
  }

  function initAdminUsersInsights() {
    if (currentPath() !== "/admin_users") return;

    var table = document.querySelector("table.msb-table");
    if (!table) return;
    if (document.getElementById("admin-users-overview")) return;

    var rows = table.querySelectorAll("tbody tr");
    if (!rows.length) return;

    var total = rows.length;
    var withEth = 0;
    var withEmail = 0;

    for (var i = 0; i < rows.length; i++) {
      var row = rows[i];
      var tds = row.querySelectorAll("td");
      if (tds.length < 5) continue;
      var username = String(tds[1].textContent || "").trim().toLowerCase();
      var email = String(tds[2].textContent || "").trim().toLowerCase();
      var eth = String(tds[4].textContent || "").trim();
      var hasEth = eth.length > 0;
      var hasEmail = email.length > 0;
      if (hasEth) withEth++;
      if (hasEmail) withEmail++;
      row.setAttribute("data-row-search", username + " " + email);
      row.setAttribute("data-has-eth", hasEth ? "1" : "0");
    }

    var section = document.createElement("section");
    section.className = "grouped-region";
    section.id = "admin-users-overview";
    section.innerHTML =
      '<h3>Users overview</h3>' +
      '<div class="msb-kpi-grid">' +
      '<div class="msb-kpi"><div class="msb-kpi-label">Users total</div><div class="msb-kpi-value">' + total + '</div></div>' +
      '<div class="msb-kpi"><div class="msb-kpi-label">Users visible</div><div class="msb-kpi-value" id="admin-users-visible-count">' + total + '</div></div>' +
      '<div class="msb-kpi"><div class="msb-kpi-label">Users with ETH</div><div class="msb-kpi-value">' + withEth + '</div></div>' +
      '<div class="msb-kpi"><div class="msb-kpi-label">Users without ETH</div><div class="msb-kpi-value">' + (total - withEth) + '</div></div>' +
      '<div class="msb-kpi"><div class="msb-kpi-label">Users with email</div><div class="msb-kpi-value">' + withEmail + '</div></div>' +
      '</div>' +
      '<div class="msb-inline-fields">' +
      '<input id="admin-users-filter" type="text" placeholder="Type username/email to filter users">' +
      '<select id="admin-users-eth-filter">' +
      '<option value="all">All users</option>' +
      '<option value="with_eth">Only with ETH</option>' +
      '<option value="without_eth">Only without ETH</option>' +
      '</select>' +
      '</div>' +
      '<div id="admin-users-visible-text" class="field-description">Showing ' + total + ' user(s) after filters.</div>';

    var tableWrap = table.closest(".msb-table-wrap");
    if (tableWrap && tableWrap.parentNode) tableWrap.parentNode.insertBefore(section, tableWrap);
    else if (table.parentNode) table.parentNode.insertBefore(section, table);

    var filterInput = document.getElementById("admin-users-filter");
    var ethSelect = document.getElementById("admin-users-eth-filter");
    var visibleCount = document.getElementById("admin-users-visible-count");
    var visibleText = document.getElementById("admin-users-visible-text");

    var apply = function () {
      var needle = String((filterInput && filterInput.value) || "").trim().toLowerCase();
      var ethMode = String((ethSelect && ethSelect.value) || "all");
      var visible = 0;
      for (var i = 0; i < rows.length; i++) {
        var row = rows[i];
        var rowSearch = row.getAttribute("data-row-search") || "";
        var rowHasEth = row.getAttribute("data-has-eth") === "1";
        var show = (!needle || rowSearch.indexOf(needle) !== -1);
        if (show && ethMode === "with_eth" && !rowHasEth) show = false;
        if (show && ethMode === "without_eth" && rowHasEth) show = false;
        row.style.display = show ? "" : "none";
        if (show) visible++;
      }
      if (visibleCount) visibleCount.textContent = String(visible);
      if (visibleText) visibleText.textContent = "Showing " + visible + " user(s) after filters.";
    };

    if (filterInput) filterInput.addEventListener("input", apply);
    if (ethSelect) ethSelect.addEventListener("change", apply);
    apply();
  }

  function initSignupTermsGate() {
    if (currentPath() !== "/signup") return;

    var form = document.querySelector("form[action='signup_post'], form[action='/signup_post']");
    if (!form) return;
    if (document.getElementById("msb-signup-terms-accepted")) return;

    var submit = form.querySelector("input[type='submit'], button[type='submit']");
    if (!submit) return;

    var row = document.createElement("div");
    row.className = "msb-signup-terms-row";

    var checkbox = document.createElement("input");
    checkbox.type = "checkbox";
    checkbox.id = "msb-signup-terms-accepted";
    checkbox.className = "msb-signup-terms-checkbox";

    var label = document.createElement("label");
    label.className = "msb-signup-terms-label";
    label.setAttribute("for", checkbox.id);
    label.appendChild(document.createTextNode("I agree to the "));

    var termsLink = document.createElement("a");
    termsLink.href = "/terms";
    termsLink.target = "_blank";
    termsLink.rel = "noopener";
    termsLink.textContent = "Terms";
    label.appendChild(termsLink);

    var toggleSubmit = function () {
      var allowed = !!checkbox.checked;
      submit.disabled = !allowed;
      if (allowed) submit.classList.remove("msb-disabled-submit");
      else submit.classList.add("msb-disabled-submit");
    };

    checkbox.addEventListener("change", toggleSubmit);
    form.addEventListener("submit", function (e) {
      if (checkbox.checked) return;
      e.preventDefault();
      window.alert("You must accept Terms to sign up.");
    });

    if (submit.parentNode) submit.parentNode.insertBefore(row, submit);
    row.appendChild(submit);
    row.appendChild(checkbox);
    row.appendChild(label);
    toggleSubmit();
  }

  function initRootCookieBanner() {
    if (currentPath() !== "/") return;
    if (document.getElementById("msb-cookie-banner")) return;

    var storageKey = "msb-cookie-consent-v1";
    try {
      if (window.localStorage.getItem(storageKey) === "accepted") return;
    } catch (e) {
      // continue showing banner if localStorage is unavailable
    }

    var banner = document.createElement("div");
    banner.id = "msb-cookie-banner";
    banner.className = "msb-cookie-banner";

    var text = document.createElement("div");
    text.className = "msb-cookie-text";
    text.textContent = "This website uses cookies for login, language and basic security.";

    var actions = document.createElement("div");
    actions.className = "msb-cookie-actions";

    var okButton = document.createElement("button");
    okButton.type = "button";
    okButton.className = "msb-cookie-ok";
    okButton.textContent = "OK";

    var moreLink = document.createElement("a");
    moreLink.href = "/terms";
    moreLink.className = "msb-cookie-more";
    moreLink.textContent = "Learn more";

    okButton.addEventListener("click", function () {
      try {
        window.localStorage.setItem(storageKey, "accepted");
      } catch (e) {
        // ignore
      }
      if (banner.parentNode) banner.parentNode.removeChild(banner);
    });

    actions.appendChild(okButton);
    actions.appendChild(moreLink);
    banner.appendChild(text);
    banner.appendChild(actions);
    document.body.appendChild(banner);
  }

  function removeFooterBotStatusLink() {
    var links = document.querySelectorAll(".footer a[href='/bot_status']");
    for (var i = 0; i < links.length; i++) {
      var link = links[i];
      var parent = link.parentNode;
      if (!parent) continue;

      var prev = link.previousSibling;
      if (prev && prev.nodeType === 3 && /\|\s*$/.test(prev.textContent || "")) {
        parent.removeChild(prev);
      } else {
        var next = link.nextSibling;
        if (next && next.nodeType === 3) {
          next.textContent = String(next.textContent || "").replace(/^\s*\|\s*/, " ");
        }
      }
      parent.removeChild(link);
    }
  }

  function replacePageContent(html) {
    var header = document.querySelector("header");
    if (!header || !header.parentNode) return;

    var footerHr = null;
    var footers = document.querySelectorAll("div.footer");
    if (footers.length) {
      var cursor = footers[0].previousSibling;
      while (cursor) {
        if (cursor.nodeType === 1 && cursor.tagName === "HR") {
          footerHr = cursor;
          break;
        }
        cursor = cursor.previousSibling;
      }
    }
    if (!footerHr) return;

    var node = header.nextSibling;
    while (node && node !== footerHr) {
      var next = node.nextSibling;
      if (!(node.nodeType === 1 && node.id === "msb-lang-toggle")) {
        if (node.parentNode) node.parentNode.removeChild(node);
      }
      node = next;
    }

    var wrap = document.createElement("div");
    wrap.id = "msb-page-override";
    wrap.className = "main";
    wrap.innerHTML = html;
    footerHr.parentNode.insertBefore(wrap, footerHr);
  }

  function hidePageTopHeader() {
    var header = document.querySelector("header");
    if (!header) return;
    var h1 = header.querySelector("h1");
    if (h1) h1.textContent = "";
    header.style.display = "none";
  }

  function overrideTermsPage() {
    if (currentPath() !== "/terms") return;
    hidePageTopHeader();
    document.title = "Условия использования";
    replacePageContent(
      '<h1>Metasiberia</h1>' +
      '<h2>Условия обслуживания</h2>' +
      '<p>Эти условия обслуживания применяются к веб-сайту metasiberia (по адресу vr.metasiberia.com) и виртуальному миру Metasiberia, который размещен на серверах Reg.ru и доступен через клиентское программное обеспечение.</p>' +
      '<p>Они вместе составляют «Сервис».</p>' +
      '<h2>Общие условия</h2>' +
      '<p>Получая доступ или используя «Сервис», вы соглашаетесь соблюдать эти Условия.</p>' +
      '<p>Если вы не согласны с какой-либо частью условий, вы не можете получить доступ к Сервису.</p>' +
      '<p>Не допускается порнография и насилие.</p>' +
      '<p>Содержимое парселя не должно серьезно и неблагоприятно влиять на производительность или функционирование сервера(ов) Metasiberia или клиента. (Например, не загружайте модели с чрезмерным количеством полигонов или разрешением текстур)</p>' +
      '<p>Не пытайтесь намеренно вывести из строя или ухудшить работу сервера или клиентов других пользователей.</p>' +
      '<p>Мы оставляем за собой право отказать в обслуживании любому человеку в любое время и по любой причине.</p>' +
      '<p>Мы оставляем за собой право изменять условия обслуживания.</p>'
    );
  }

  function overrideFAQPage() {
    if (currentPath() !== "/faq") return;
    hidePageTopHeader();
    document.title = "Основные вопросы о Metasiberia";
    replacePageContent(
      '<h1>Основные вопросы о Metasiberia</h1>' +
      '<h2>Что такое Metasiberia?</h2>' +
      '<p>Metasiberia — это метавселеная. Здесь пользователи могут исследовать бескрайние просторы, общаться, играть и создавать свои уникальные территории в метавселенной.</p>' +
      '<h2>Как войти в Метасибирь?</h2>' +
      '<p>Адрес в клиенте: <a href="sub://vr.metasiberia.com">sub://vr.metasiberia.com</a></p>' +
      '<p>Веб-режим: <a href="https://vr.metasiberia.com/webclient">https://vr.metasiberia.com/webclient</a></p>' +
      '<p>Зарегистрируйтесь: <a href="https://vr.metasiberia.com/signup">https://vr.metasiberia.com/signup</a></p>' +
      '<ol>' +
      '<li>Скачайте и установите клиент виртуальных миров Substrata: Тут.</li>' +
      '<li>В адресной строке введите <code>sub://vr.metasiberia.com</code> и нажмите Enter.</li>' +
      '<li>В клиенте нажмите Log in (правый верхний угол) и введите логин/пароль.</li>' +
      '<li>Назначьте Metasiberia стартовой локацией: в меню Go выберите Set current location as start location.</li>' +
      '<li>Теперь ваш аватар всегда будет появляться в центре Metasiberia.</li>' +
      '</ol>' +
      '<h2>На чём основана Metasiberia?</h2>' +
      '<p>Metasiberia — это проект, основанный на технологиях Substrata, разработанных Glare Technologies Limited. Эта же команда известна своими продуктами Indigo Renderer и Chaotica Fractals.</p>' +
      '<h2>Почему Metasiberia, это метавселенная?</h2>' +
      '<p>Metasiberia классифицируется как метавселенная, поскольку представляет собой интегрированную цифровую среду, объединяющую множество виртуальных пространств с высоким уровнем интерактивности и пользовательской автономии. С научной точки зрения, метавселенная — это устойчивая, коллективно используемая виртуальная реальность, которая функционирует как параллельная система, поддерживающая социальные, творческие и экономические взаимодействия. В Metasiberia это достигается через центральный мир, выступающий хабом для новых пользователей, и персональные территории (доступные по адресу sub://vr.metasiberia.com/имя_пользователя), где каждый может формировать собственное пространство.</p>' +
      '<h2>Как приобрести участок в Metasiberia?</h2>' +
      '<p>На данный момент покупка участков доступна только по индивидуальному запросу.</p>' +
      '<p>В ближайшее время мы запустим магазин, где участки можно будет приобрести напрямую. Следите за обновлениями на нашем сайте и в социальных сетях.</p>' +
      '<h2>Можно ли делиться правами на участок в Metasiberia?</h2>' +
      '<p>Да, вы можете добавить других пользователей как "соавторов" вашего участка. Они смогут создавать, редактировать и удалять объекты на вашей территории.</p>' +
      '<ol>' +
      '<li>Войдите в аккаунт на сайте Metasiberia/admin panel/ log in.</li>' +
      '<li>Перейдите к вашему участку на своей странице.</li>' +
      '<li>Нажмите "Add writer" и укажите имя пользователя в Metasiberia.</li>' +
      '<li>Также можно временно или постоянно открыть участок для общего редактирования, выбрав в редакторе объектов опцию "All writeable".</li>' +
      '</ol>' +
      '<h1>Создание и строительство в Metasiberia</h1>' +
      '<h2>Как создавать объекты в Metasiberia?</h2>' +
      '<p>Создавать объекты в центральном мире можно на участке, который вам принадлежит, или в песочнице (участок №31). В своей личной территории ограничений нет.</p>' +
      '<p>Есть два способа:</p>' +
      '<ol>' +
      '<li>В клиенте выберите "Добавить модель / изображение / видео" и следуйте инструкциям.</li>' +
      '<li>Создайте объект из вокселей внутри Metasiberia, выбрав "Добавить воксели".</li>' +
      '</ol>' +
      '<p>Поддерживаемые форматы:</p>' +
      '<p>Модели: OBJ, GLTF, GLB, VOX, STL, IGMESH</p>' +
      '<p>Изображения: JPG, PNG, GIF, TIF, EXR, KTX, KTX2</p>' +
      '<p>Видео: MP4</p>' +
      '<p>Пожалуйста, избегайте загрузки моделей с большим количеством полигонов или объёмных файлов, чтобы не ухудшить производительность для других пользователей.</p>' +
      '<h2>Как работают воксели в Metasiberia?</h2>' +
      '<p>Вы можете загрузить готовый воксель в поддерживаемом формате или создать его внутри Metasiberia.</p>' +
      '<p>Для создания:</p>' +
      '<ol>' +
      '<li>Перейдите на участок, где у вас есть права редактирования, и выберите "Добавить воксели". Появится первый блок (серый куб).</li>' +
      '<li>Всплывёт подсказка с инструкциями. Основное: Ctrl + левый клик добавляет новый воксель на поверхность выбранного куба. При наведении с зажатым Ctrl вы увидите, где появится новый блок.</li>' +
      '<li>Воксели можно масштабировать в редакторе, меняя их размеры через параметр "Scale".</li>' +
      '</ol>' +
      '<h2>Как анимировать объекты в Metasiberia?</h2>' +
      '<p>Анимация и скрипты в Metasiberia создаются с помощью языка программирования Lua. Подробности доступны на странице: о скриптах.</p>' +
      '<p>Для настройки скрипта выберите объект в клиенте, откройте редактор и в разделе "Script" введите код. Редактировать можно только свои объекты.</p>' +
      '<h2>Есть ли ограничения на контент в Metasiberia?</h2>' +
      '<p>Да, контент "не для всех" (например, с сексуальным или насильственным подтекстом) запрещён. Подробности — в правилах использования.</p>' +
      '<h1>Решение проблем</h1>' +
      '<h2>У меня сложности с Metasiberia — где я могу получить помощь?</h2>' +
      '<p>Лучше всего обратиться за помощью в наш <a href="https://vk.com/metasiberia_official" target="_blank" rel="noopener">Vk</a> или <a href="https://t.me/metasiberia_channel" target="_blank" rel="noopener">Telegram</a>.</p>' +
      '<p><b>Metasiberia состоит из Substrata.</b></p>'
    );
  }

  document.addEventListener("DOMContentLoaded", function () {
    applyGlobalThemeClass();
    applyPageClasses();
    enhanceAdminLayout();
    initAdminParcelLivePreview();
    ensureAdminParcelsLegacyLayout();
    initAdminParcelsLocalFilter();
    initAdminParcelsTweakpane();
    initAdminUsersInsights();
    initSignupTermsGate();
    initRootCookieBanner();
    removeFooterBotStatusLink();
    overrideTermsPage();
    overrideFAQPage();
    ensureLangToggleButton();
    initAccordions();
    initSliders();

    var lang = getLang();
    applyLanguage(lang);
    ensureTooltips(lang);
  });
})();
