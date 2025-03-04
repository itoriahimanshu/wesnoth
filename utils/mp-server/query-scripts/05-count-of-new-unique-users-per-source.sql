select count(*) as NEW_USERS_FIRST_GAME, CLIENT_SOURCE
from
(
    select player.USER_ID, min(game.START_TIME) as FIRST_GAME_START, CLIENT_SOURCE
    from wesnothd_game_info game, wesnothd_game_player_info player
    where game.INSTANCE_UUID = player.INSTANCE_UUID
      and game.GAME_ID = player.GAME_ID
      and player.CLIENT_SOURCE != ''
    group by player.USER_ID, CLIENT_SOURCE
) as temp
where YEAR(FIRST_GAME_START) = YEAR(CURRENT_DATE - INTERVAL 2 MONTH)
  and MONTH(FIRST_GAME_START) = MONTH(CURRENT_DATE - INTERVAL 2 MONTH)
group by CLIENT_SOURCE
