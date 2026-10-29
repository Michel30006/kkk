# IQs propios de ToDus (API XMPP de control)

Cada IQ es un `<iq>` de XMPP cuyo hijo lleva `ELEMENT` como nombre local
y `NAMESPACE` como valor de `xmlns`.

| IQ | ELEMENT | NAMESPACE |
|---|---|---|
| `block/BlockUserIQ` | `query` | `todus:block:set` |
| `block/GetBlockListIQ` | `query` | `todus:block:get:2` |
| `block/GetBlockListPaginatedIQ` | `query` | `todus:block:get:3` |
| `block/UnBlockUserIQ` | `query` | `todus:block:unset:2` |
| `call/CallStatusIQ` | `query` | `todus:call:status` |
| `call/GetCredentialsIQ` | `query` | `todus:turn:cred` |
| `channels/AddSubsToChannelIQ` | `query` | `todus:ch:add_subs` |
| `channels/ChannelInfoIQById` | `query` | `todus:ch:info:id` |
| `channels/ChannelInfoIQByLink` | `query` | `todus:ch:info:link:v2` |
| `channels/CreateChannelIQ` | `query` | `todus:ch:create` |
| `channels/DeletePubFromChannelIQ` | `query` | `todus:ch:delpub` |
| `channels/DeleteSubscriberChannelIQ` | `query` | `todus:ch:removesubscriber` |
| `channels/DemoteAdminChannelIQ` | `query` | `todus:ch:demote` |
| `channels/EditAdminRightsIQ` | `query` | `todus:ch:update:rights` |
| `channels/EditInfoChannelIQ` | `query` | `todus:ch:edit` |
| `channels/EditPubChannelIQ` | `query` | `todus:ch:edit:pub` |
| `channels/GetChannelAdminsIQ` | `query` | `todus:ch:getadmins` |
| `channels/GetChannelListIQ` | `query` | `todus:ch:my_channels` |
| `channels/GetChannelListPaginatedIQ` | `query` | `todus:ch:my_channels:2` |
| `channels/GetChannelSubscribersIQ` | `query` | `todus:ch:getsubscribers:2` |
| `channels/GetPubByIdIQ` | `query` | `todus:ch:getpub:id` |
| `channels/GetPublicationsChannelIQ` | `query` | `todus:ch:getpub` |
| `channels/LeaveChannelIQ` | `query` | `todus:ch:leave` |
| `channels/NotificationChannelEditedIQ` | `query` | `todus:ch:updated_info` |
| `channels/PromoteToAdminChannelIQ` | `query` | `todus:ch:promote` |
| `channels/PublishToChannelIQ` | `query` | `todus:ch:publish` |
| `channels/SubscribeToChannelIQ` | `query` | `todus:ch:subscribe` |
| `error/ToDusErrorIQ` | `error` | `todus:error` |
| `groups/AddGroupMembersIQ` | `query` | `td:g:add_occupant` |
| `groups/AffiliationsChangeExtension` | `x` | `x11` |
| `groups/ChangeGroupConfigsIQ` | `query` | `x10` |
| `groups/ConfigurationsChangeExtension` | `x` | `x10` |
| `groups/CreateGroupIQ` | `query` | `x16` |
| `groups/DemoteGroupAdminIQ` | `query` | `td:g:demote` |
| `groups/ExpelMemberFromGroupIQ` | `query` | `x11` |
| `groups/GetGroupListPaginatedIQ` | `query` | `todus:muclight:my_mucs:2` |
| `groups/GroupGetLinkIQ` | `query` | `x14` |
| `groups/GroupInfoByIdIQ` | `query` | `td:g:info_by_id` |
| `groups/GroupInfoIQByLink` | `query` | `td:g:info_by_link` |
| `groups/GroupMembersIQ` | `query` | `x11` |
| `groups/GroupSetLinkIQ` | `query` | `x14` |
| `groups/JoinGroupIQ` | `query` | `td:g:join` |
| `groups/LeaveGroupIQ` | `query` | `x13` |
| `groups/PromoteGroupAdminIQ` | `query` | `td:g:promote` |
| `last/LastIQ` | `query` | `todus:last:2` |
| `near/NearGetStatusIQ` | `query` | `tnearp:status:get` |
| `near/NearHideIQ` | `query` | `tnearp:loc:hide` |
| `near/NearSetIQ` | `query` | `tnearp:loc:set` |
| `near/PeopleNearIQ` | `query` | `tnearp:find` |
| `offline/GetOfflineIQ` | `query` | `t:offline` |
| `offline/OfflineElement` | `todus_offline` | `jc` |
| `offline/SendOfflineIdIQ` | `query` | `t:offline:del` |
| `packet/Message` | `m` | `—` |
| `privacy/PrivacyAddGetIQ` | `query` | `todus:group:privacy` |
| `privacy/PrivacyAddUpdateIQ` | `query` | `todus:group:privacy` |
| `privacy/PrivacyGetIQ` | `query` | `todus:privacy` |
| `privacy/PrivacyUpdateIQ` | `query` | `todus:privacy` |
| `push/ToDusPushIQ` | `query` | `todus:fb:push` |
| `roster/RosterDeleteIQ` | `query` | `todus:roster:delete` |
| `roster/RosterEditToDusIdIQ` | `query` | `todus:users:updatetodusid` |
| `roster/RosterGetHashIQ` | `query` | `todus:roster:hash` |
| `roster/RosterGetListPaginatedIQ` | `query` | `todus:roster:list:2` |
| `roster/RosterGetToDusIdIQ` | `query` | `todus:users:gettodusid` |
| `roster/RosterGetUsersInfoIQ` | `query` | `todus:users:getinfo` |
| `roster/RosterGetVersionIQ` | `query` | `todus:users:getrosterversion` |
| `roster/RosterUpdateIQ` | `query` | `todus:roster:update` |
| `roster/RosterUpdateVersionIQ` | `query` | `todus:users:updaterosterversion` |
| `s3/GetUrlIQ` | `query` | `todus:gurl` |
| `s3/PutUrlIQ` | `query` | `todus:purl` |
| `status/DeleteStatusIQ` | `query` | `td:status:delete` |
| `status/FollowIQ` | `query` | `td:status:follow` |
| `status/GetFollowInfoIQ` | `query` | `td:status:follower:info` |
| `status/GetFollowersIQ` | `query` | `td:status:followers` |
| `status/GetFollowingIQ` | `query` | `td:status:following` |
| `status/GetStatusIQ` | `query` | `td:status:get` |
| `status/PublishStatusIQ` | `query` | `td:status:publish` |
| `status/UnFollowIQ` | `query` | `td:status:unfollow` |
| `version/GetUpdateInfoIQ` | `query` | `todus:version:getupdateinfo` |

**Total de IQs:** 78

## Detalle de constructores y getters

- **block/BlockUserIQ** (`query` / `todus:block:set`): `getCode`, `getUser`
- **block/GetBlockListIQ** (`query` / `todus:block:get:2`): `getBlock`, `getCode`
- **block/GetBlockListPaginatedIQ** (`query` / `todus:block:get:3`): `getBlock`, `getCode`, `getLimit`, `getOffset`
- **block/UnBlockUserIQ** (`query` / `todus:block:unset:2`): `getCode`, `getUser`
- **call/CallStatusIQ** (`query` / `todus:call:status`): `getContent`, `getFromUser`, `getStatus`, `getToUser`
- **call/GetCredentialsIQ** (`query` / `todus:turn:cred`): `getTurnCredentials`
- **channels/AddSubsToChannelIQ** (`query` / `todus:ch:add_subs`): `getLink`, `getSubs`
- **channels/ChannelInfoIQById** (`query` / `todus:ch:info:id`): `getChannelInfo`, `getId`
- **channels/ChannelInfoIQByLink** (`query` / `todus:ch:info:link:v2`): `getChannelInfo`, `getLink`
- **channels/CreateChannelIQ** (`query` / `todus:ch:create`): `getChId`, `getDesc`, `getLink`, `getName`, `getPicture`, `getPub`, `getSubs`
- **channels/DeletePubFromChannelIQ** (`query` / `todus:ch:delpub`): `getChId`, `getPublIDs`
- **channels/DeleteSubscriberChannelIQ** (`query` / `todus:ch:removesubscriber`): `getChId`, `getSub`
- **channels/DemoteAdminChannelIQ** (`query` / `todus:ch:demote`): `getChId`, `getSub`
- **channels/EditAdminRightsIQ** (`query` / `todus:ch:update:rights`): `getChId`, `getRights`, `getSub`
- **channels/EditInfoChannelIQ** (`query` / `todus:ch:edit`): `getId`, `getLink`, `getName`, `getProfilePhoto`, `getProfilePhotoThumb`, `getPublic`, `getSubject`
- **channels/EditPubChannelIQ** (`query` / `todus:ch:edit:pub`): `getChId`, `getPub`, `getPublID`
- **channels/GetChannelAdminsIQ** (`query` / `todus:ch:getadmins`): `getAdmins`, `getId`
- **channels/GetChannelListIQ** (`query` / `todus:ch:my_channels`): `getChId`
- **channels/GetChannelListPaginatedIQ** (`query` / `todus:ch:my_channels:2`): `getChId`, `getLimit`, `getOffset`
- **channels/GetChannelSubscribersIQ** (`query` / `todus:ch:getsubscribers:2`): `getId`, `getLimit`, `getOffset`, `getSubscribers`
- **channels/GetPubByIdIQ** (`query` / `todus:ch:getpub:id`): `getChId`, `getPubEdited`, `getPubId`
- **channels/GetPublicationsChannelIQ** (`query` / `todus:ch:getpub`): `getId`, `getLast_id`, `getLimit`, `getPubList`
- **channels/LeaveChannelIQ** (`query` / `todus:ch:leave`): `getChId`
- **channels/NotificationChannelEditedIQ** (`query` / `todus:ch:updated_info`): `getCh`, `getLink`
- **channels/PromoteToAdminChannelIQ** (`query` / `todus:ch:promote`): `getChId`, `getRights`, `getSub`
- **channels/PublishToChannelIQ** (`query` / `todus:ch:publish`): `getLink`, `getPubl`
- **channels/SubscribeToChannelIQ** (`query` / `todus:ch:subscribe`): `getLink`
- **error/ToDusErrorIQ** (`error` / `todus:error`): `getCode`, `getMessage`
- **groups/AddGroupMembersIQ** (`query` / `td:g:add_occupant`): `getCode`, `getNewMembers`
- **groups/AffiliationsChangeExtension** (`x` / `x11`): `getAffiliations`, `getPrevVersion`, `getVersion`
- **groups/ChangeGroupConfigsIQ** (`query` / `x10`): —
- **groups/ConfigurationsChangeExtension** (`x` / `x10`): `getCustomConfigs`, `getPrevVersion`, `getRoomName`, `getSubject`, `getVersion`
- **groups/CreateGroupIQ** (`query` / `x16`): `getCustomConfigs`, `getNewOccupants`, `getRoomName`, `getSubject`
- **groups/DemoteGroupAdminIQ** (`query` / `td:g:demote`): `getAdmin`, `getCode`
- **groups/ExpelMemberFromGroupIQ** (`query` / `x11`): `getAffiliations`
- **groups/GetGroupListPaginatedIQ** (`query` / `todus:muclight:my_mucs:2`): `getLimit`, `getMucs`, `getOffset`
- **groups/GroupGetLinkIQ** (`query` / `x14`): `getLink`
- **groups/GroupInfoByIdIQ** (`query` / `td:g:info_by_id`): `getMucInfo`
- **groups/GroupInfoIQByLink** (`query` / `td:g:info_by_link`): `getLinkId`, `getMucInfo`
- **groups/GroupMembersIQ** (`query` / `x11`): `getMembers`
- **groups/GroupSetLinkIQ** (`query` / `x14`): —
- **groups/JoinGroupIQ** (`query` / `td:g:join`): `getLink`
- **groups/LeaveGroupIQ** (`query` / `x13`): —
- **groups/PromoteGroupAdminIQ** (`query` / `td:g:promote`): `getCode`, `getMember`
- **last/LastIQ** (`query` / `todus:last:2`): `getLast`, `getUid`
- **near/NearGetStatusIQ** (`query` / `tnearp:status:get`): —
- **near/NearHideIQ** (`query` / `tnearp:loc:hide`): `getCode`
- **near/NearSetIQ** (`query` / `tnearp:loc:set`): `getCode`, `getLat`, `getLon`
- **near/PeopleNearIQ** (`query` / `tnearp:find`): `getLimit`, `getOffset`, `getUsers`
- **offline/GetOfflineIQ** (`query` / `t:offline`): —
- **offline/OfflineElement** (`todus_offline` / `jc`): `getStamp`
- **offline/SendOfflineIdIQ** (`query` / `t:offline:del`): —
- **packet/Message** (`m` / `—`): `getBodies`, `getBody`, `getBodyLanguages`, `getExtension`, `getExtensions`, `getId`, `getSubject`, `getSubjectLanguages`, `getSubjects`, `getThread`
- **privacy/PrivacyAddGetIQ** (`query` / `todus:group:privacy`): `getExceptions`, `getWhoCan`
- **privacy/PrivacyAddUpdateIQ** (`query` / `todus:group:privacy`): `getExceptions`, `getWhoCan`
- **privacy/PrivacyGetIQ** (`query` / `todus:privacy`): `getInfo`, `getLast`, `getProfilePhoto`
- **privacy/PrivacyUpdateIQ** (`query` / `todus:privacy`): `getInfo`, `getLast`, `getProfilePhoto`
- **push/ToDusPushIQ** (`query` / `todus:fb:push`): `getId`
- **roster/RosterDeleteIQ** (`query` / `todus:roster:delete`): `getContacts`
- **roster/RosterEditToDusIdIQ** (`query` / `todus:users:updatetodusid`): `getCode`, `getToDusId`
- **roster/RosterGetHashIQ** (`query` / `todus:roster:hash`): `getHash`
- **roster/RosterGetListPaginatedIQ** (`query` / `todus:roster:list:2`): `getContacts`, `getLimit`, `getOffset`
- **roster/RosterGetToDusIdIQ** (`query` / `todus:users:gettodusid`): `getMinVersionAllowed`, `getToDusId`, `getUsername`
- **roster/RosterGetUsersInfoIQ** (`query` / `todus:users:getinfo`): `getNumbers`, `getUsers`
- **roster/RosterGetVersionIQ** (`query` / `todus:users:getrosterversion`): `getRosterVersion`, `getUser`
- **roster/RosterUpdateIQ** (`query` / `todus:roster:update`): `getCode`, `getContacts`
- **roster/RosterUpdateVersionIQ** (`query` / `todus:users:updaterosterversion`): `getCode`, `getRosterVersion`
- **s3/GetUrlIQ** (`query` / `todus:gurl`): `getDu`, `getStatus`, `getUrl`
- **s3/PutUrlIQ** (`query` / `todus:purl`): `getFileType`, `getGet`, `getPersistent`, `getPut`, `getRoom`, `getSize`, `getStatus`
- **status/DeleteStatusIQ** (`query` / `td:status:delete`): `getCode`
- **status/FollowIQ** (`query` / `td:status:follow`): `getCode`
- **status/GetFollowInfoIQ** (`query` / `td:status:follower:info`): `getCode`, `getFollowsCount`, `getState`
- **status/GetFollowersIQ** (`query` / `td:status:followers`): `getError`, `getFollowers`, `getLimit`, `getOffset`, `getUid`
- **status/GetFollowingIQ** (`query` / `td:status:following`): `getError`, `getFollowing`, `getLimit`, `getOffset`, `getUid`
- **status/GetStatusIQ** (`query` / `td:status:get`): `getCode`, `getJsonStatusContent`
- **status/PublishStatusIQ** (`query` / `td:status:publish`): `getCode`
- **status/UnFollowIQ** (`query` / `td:status:unfollow`): `getCode`
- **version/GetUpdateInfoIQ** (`query` / `todus:version:getupdateinfo`): `getDownloadUrl`, `getMinRequiredVersionCode`, `getPackageName`, `getPublishedAt`, `getVersionCode`, `getVersionName`
