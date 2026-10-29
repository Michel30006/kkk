# Superficie HTTP de ToDus (Retrofit)

Extraído del smali de la APK v3.0.2 (`cu.todus.android`, 2026-10-20).

Los verbos y tipos de parámetro **no se infieren**: se leen del
`RequestFactory` de Retrofit incluido en la propia APK, que resuelve
las anotaciones ofuscadas por R8 (`Lretrofit2/http/POST;` → `Loi/o;`).

## cu.todus.android.rest.auth.AuthService

| Método | Verbo | Ruta | Parámetros |
|---|---|---|---|
| `linkInfo` | **POST** | `v3/preview` | @Body LinkInfoRequest |
| `passCheck` | **POST** | `v2/auth/check` | @Body ApiProto$ProtoAuthCheckRequest |
| `register` | **POST** | `v2/auth/users.register` | @Body ApiProto$ProtoUserRegisterRequest |
| `reserve` | **POST** | `v2/auth/users.reserve` | @Body ApiProto$ProtoUserReserveRequest |
| `token` | **POST** | `v2/auth/token` | @Body ApiProto$ProtoUserAuthTokenRequest |
| `tokenNotLive` | **POST** | `v2/auth/token` | @Body ApiProto$ProtoUserAuthTokenRequest |

## cu.todus.android.rest.catalogue.CatalogueService

| Método | Verbo | Ruta | Parámetros |
|---|---|---|---|
| `addProduct` | **PUT** | `product` | @Body AddProductRequest |
| `deleteProduct` | **DELETE** | `product/{id}` | - e |
| `getProduct` | **GET** | `product/{todus_id}/{id}` | @Path String |
| `myShop` | **GET** | `shop/{limit}/{offset}` | - e |
| `requestProduct` | **GET** | `request/product/{id}` | - e |
| `searchProductsByTag` | **GET** | `search/products/tag/{todus_id}/{tag}/{limit}/{offset}` | @Path String, @Path String |
| `searchTags` | **GET** | `search/tags/{todus_id}` | @Path String |
| `shareCatalog` | **GET** | `share/product/{id}` | - e |
| `shareProduct` | **GET** | `share/product/{id}` | - e |
| `shop` | **GET** | `{todus_id}/{limit}/{offset}` | @Path String |
| `updateProduct` | **PATCH** | `product/{id}` | @Body UpdateProductRequest |
| `version` | **GET** | `version` | - e |

## cu.todus.android.rest.certified.CertifiedInfoService

| Método | Verbo | Ruta | Parámetros |
|---|---|---|---|
| `getInfo` | **GET** | `card/info/{uid}` | @Path String |
| `getInfoOrError` | **GET** | `info/{uid}` | @Path String |
| `subscribe` | **GET** | `card/subscribe/{uid}` | @Path String |
| `unSubscribe` | **GET** | `card/unsubscribe/{uid}` | @Path String |

## cu.todus.android.rest.client.ClientService

| Método | Verbo | Ruta | Parámetros |
|---|---|---|---|
| `downloadLink` | **POST** | `https://api.apklis.cu/v2/release/get_url/` | @Body LinkRequest |
| `lastBetaFromToDus` | **GET** | `todus/release/bet/latest` | - e |
| `lastRelease` | **GET** | `https://api.apklis.cu/v1/application/338/` | - e |
| `lastReleaseFromToDus` | **GET** | `todus/release/rel/latest` | - e |

## cu.todus.android.rest.linkedDevice.LinkedDeviceService

| Método | Verbo | Ruta | Parámetros |
|---|---|---|---|
| `getLinkedDevices` | **GET** | `list` | - e |
| `linkDevice` | **GET** | `` | @Url String |
| `logoutDevice` | **POST** | `logout` | @Body LinkedDeviceLogoutRequest |

## cu.todus.android.rest.report.ReportService

| Método | Verbo | Ruta | Parámetros |
|---|---|---|---|
| `reportMessage` | **POST** | `v1/report` | @Body ReportMessageRequest |

## cu.todus.android.rest.sticker.StickerService

| Método | Verbo | Ruta | Parámetros |
|---|---|---|---|
| `packInfo` | **GET** | `v1/stickers/info/{pack_name}` | @Path String |
| `packs` | **GET** | `v1/stickers/packs/{limit}/{offset}` | @Query String |

## cu.todus.android.rest.user.UserService

| Método | Verbo | Ruta | Parámetros |
|---|---|---|---|
| `updateProfile` | **POST** | `v2/todus/users.me` | @Body ApiProto$ProtoUsersMeRequest |

**Total de operaciones:** 33
