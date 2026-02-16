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

  document.addEventListener("DOMContentLoaded", function () {
    ensureLangToggleButton();
    initAccordions();
    initSliders();

    var lang = getLang();
    applyLanguage(lang);
    ensureTooltips(lang);
  });
})();
