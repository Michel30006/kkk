# Esquema de la base de datos local (Room) — ToDus v2.1.2

Extraído de `ToDusDatabase_Impl.createAllTables()` (código generado por Room).
Base de datos: `ToDusDatabase` (caché local de mensajes, salas, usuarios y estados).

```sql
CREATE INDEX IF NOT EXISTS `index_attachments_msg` ON `attachments` (`msg`)
CREATE INDEX IF NOT EXISTS `index_marker_msg` ON `marker` (`msg`)
CREATE INDEX IF NOT EXISTS `index_messages_id` ON `messages` (`id`)
CREATE INDEX IF NOT EXISTS `index_messages_roomId` ON `messages` (`roomId`)
CREATE INDEX IF NOT EXISTS `index_rooms_id` ON `rooms` (`id`)
CREATE INDEX IF NOT EXISTS `index_user_room_join_roomId` ON `user_room_join` (`roomId`)
CREATE INDEX IF NOT EXISTS `messages_createAt_index` ON `messages` (`createAt`)
CREATE INDEX IF NOT EXISTS `rooms_pined_lastUpdate_index` ON `rooms` (`pined`, `lastUpdate`)
CREATE TABLE IF NOT EXISTS `Contact` (`phone` TEXT NOT NULL, `uid` TEXT NOT NULL, `fName` TEXT NOT NULL, `phoneType` TEXT NOT NULL, `photoUri` TEXT NOT NULL, `state` TEXT, PRIMARY KEY(`phone`))
CREATE TABLE IF NOT EXISTS `attachments` (`id` TEXT NOT NULL, `msg` TEXT NOT NULL, `content` TEXT NOT NULL, `state` INTEGER NOT NULL, `type` INTEGER NOT NULL, `persistent` INTEGER NOT NULL, PRIMARY KEY(`id`), FOREIGN KEY(`msg`) REFERENCES `messages`(`id`) ON UPDATE CASCADE ON DELETE CASCADE )
CREATE TABLE IF NOT EXISTS `heritage` (`tag` TEXT NOT NULL, `alert` INTEGER NOT NULL, `sound` TEXT NOT NULL, `priority` INTEGER NOT NULL, `color` INTEGER NOT NULL, PRIMARY KEY(`tag`))
CREATE TABLE IF NOT EXISTS `linked_device` (`session_id` TEXT NOT NULL, `device` TEXT NOT NULL, `url` TEXT NOT NULL, `last_time` INTEGER NOT NULL, `state` INTEGER NOT NULL, PRIMARY KEY(`session_id`))
CREATE TABLE IF NOT EXISTS `marker` (`uid` TEXT NOT NULL, `msg` TEXT NOT NULL, `room` TEXT NOT NULL, `marker` INTEGER NOT NULL, `createdAt` INTEGER NOT NULL, PRIMARY KEY(`uid`), FOREIGN KEY(`msg`) REFERENCES `messages`(`id`) ON UPDATE CASCADE ON DELETE CASCADE )
CREATE TABLE IF NOT EXISTS `message_chunks` (`originalMsgId` TEXT NOT NULL, `chunkIndex` INTEGER NOT NULL, `textContent` TEXT NOT NULL, PRIMARY KEY(`originalMsgId`, `chunkIndex`))
CREATE TABLE IF NOT EXISTS `messages` (`id` TEXT NOT NULL, `roomId` TEXT NOT NULL, `userSender` TEXT NOT NULL, `content` TEXT NOT NULL, `createAt` INTEGER NOT NULL, `state` INTEGER NOT NULL, `payload` TEXT, `messageEntities` TEXT NOT NULL, `publId` TEXT, PRIMARY KEY(`id`), FOREIGN KEY(`roomId`) REFERENCES `rooms`(`id`) ON UPDATE CASCADE ON DELETE CASCADE )
CREATE TABLE IF NOT EXISTS `reaction` (`id` TEXT NOT NULL, `entity` TEXT NOT NULL, `userId` TEXT NOT NULL, `roomId` TEXT NOT NULL, `reactionContent` TEXT NOT NULL, `reactionState` INTEGER NOT NULL, `createdAt` INTEGER NOT NULL, PRIMARY KEY(`id`))
CREATE TABLE IF NOT EXISTS `rooms` (`id` TEXT NOT NULL, `name` TEXT NOT NULL, `type` INTEGER NOT NULL, `version` TEXT NOT NULL, `subject` TEXT NOT NULL, `config` TEXT NOT NULL, `lastUpdate` INTEGER NOT NULL, `unreadMessages` INTEGER NOT NULL, `msgId` TEXT NOT NULL, `pined` INTEGER NOT NULL, `secret` INTEGER NOT NULL, `isMuted` INTEGER NOT NULL, PRIMARY KEY(`id`))
CREATE TABLE IF NOT EXISTS `roster_change` (`id` INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL, `phone` TEXT NOT NULL, `changeType` TEXT NOT NULL, `timestamp` INTEGER NOT NULL, `previousName` TEXT, `currentName` TEXT, `synced` INTEGER NOT NULL, `targetVersion` INTEGER)
CREATE TABLE IF NOT EXISTS `status` (`id` TEXT NOT NULL, `creatorId` TEXT NOT NULL, `repliedId` TEXT NOT NULL, `createdAt` INTEGER NOT NULL, `isPublic` INTEGER NOT NULL, `isDelete` INTEGER NOT NULL, `canRespond` INTEGER NOT NULL, `type` TEXT NOT NULL, `content` TEXT NOT NULL, `seen` INTEGER NOT NULL, `closed` INTEGER NOT NULL, `messageEntities` TEXT NOT NULL, `uploadState` INTEGER NOT NULL, `location` TEXT NOT NULL, `viewCount` INTEGER NOT NULL, `interactCount` INTEGER NOT NULL, `shareCount` INTEGER NOT NULL, PRIMARY KEY(`id`))
CREATE TABLE IF NOT EXISTS `stickerPack` (`name` TEXT NOT NULL, `title` TEXT NOT NULL, `thumb` TEXT NOT NULL, `createAt` INTEGER NOT NULL, PRIMARY KEY(`name`))
CREATE TABLE IF NOT EXISTS `sticker` (`id` TEXT NOT NULL, `packName` TEXT NOT NULL, `name` TEXT NOT NULL, `folder` TEXT NOT NULL, `favorite` INTEGER NOT NULL, `lastUse` INTEGER NOT NULL, PRIMARY KEY(`id`), FOREIGN KEY(`packName`) REFERENCES `stickerPack`(`name`) ON UPDATE CASCADE ON DELETE CASCADE )
CREATE TABLE IF NOT EXISTS `user_room_join` (`userId` TEXT NOT NULL, `roomId` TEXT NOT NULL, `affiliation` TEXT NOT NULL, PRIMARY KEY(`userId`, `roomId`), FOREIGN KEY(`roomId`) REFERENCES `rooms`(`id`) ON UPDATE NO ACTION ON DELETE CASCADE )
CREATE TABLE IF NOT EXISTS `user` (`uid` TEXT NOT NULL, `username` TEXT NOT NULL, `alias` TEXT NOT NULL, `photoUrl` TEXT NOT NULL, `description` TEXT NOT NULL, `version` INTEGER NOT NULL, `photoThumbnail` TEXT NOT NULL, `official` INTEGER NOT NULL, `blocked` INTEGER NOT NULL, `toDusId` TEXT NOT NULL, PRIMARY KEY(`uid`))
CREATE TABLE IF NOT EXISTS room_master_table (id INTEGER PRIMARY KEY,identity_hash TEXT)
```

## Tablas y su propósito

| Tabla | Contenido |
|---|---|
| `messages` | Mensajes (id, sala, emisor, contenido, payload, entidades) |
| `rooms` | Salas/canales (tipo, versión, config, no leído, fijado, secreto, silenciado) |
| `user` | Usuarios (uid, alias, foto, descripción, versión, toDusId, oficial, bloqueado) |
| `user_room_join` | Membresía usuario↔sala con \*afiliación\* |
| `attachments` | Adjuntos de mensajes (estado, tipo, persistente) |
| `marker` | Marcadores de lectura (`marker`, `createdAt`) |
| `reaction` | Reacciones a mensajes |
| `status` | Estados/respuestas a mensajes (contenido, visto, cerrado, contadores) |
| `heritage` | Preferencias de tag (alerta, sonido, prioridad, color) |
| `linked_device` | Dispositivos vinculados (sesión, url, último uso, estado) |
| `roster_change` | Cola de cambios de roster pendientes de sincronizar |
| `message_chunks` | Fragmentación de mensajes largos (originalMsgId + chunkIndex) |
| `sticker` / `stickerPack` | Stickers y packs |
| `Contact` | Contactos del teléfono (teléfono, nombre, foto, estado) |
