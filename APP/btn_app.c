/* Licence
* Company: MCUSTUDIO
* Auther: Ahypnis.
* Version: V0.10
* Time: 2025/06/05
* Note:
*/
#include "mcu_cmic_gd32f470vet6.h"


extern uint8_t ucLed[6];

typedef enum
{
    USER_BUTTON_0 = 0,
    USER_BUTTON_1,
    USER_BUTTON_2,
    USER_BUTTON_3,
    USER_BUTTON_4,
    USER_BUTTON_5,
    USER_BUTTON_6,
    USER_BUTTON_MAX,

    //    USER_BUTTON_COMBO_0 = 0x100,
    //    USER_BUTTON_COMBO_1,
    //    USER_BUTTON_COMBO_2,
    //    USER_BUTTON_COMBO_3,
    //    USER_BUTTON_COMBO_MAX,
} user_button_t;

/*  Debounce time in milliseconds, Debounce time in milliseconds for release event, Minimum pressed time for valid click event, Maximum ...,
    Maximum time between 2 clicks to be considered consecutive click, Time in ms for periodic keep alive event, Max number of consecutive clicks */
static const ebtn_btn_param_t defaul_ebtn_param = EBTN_PARAMS_INIT(20, 0, 20, 1000, 0, 1000, 10);

static ebtn_btn_t btns[] = {
    EBTN_BUTTON_INIT(USER_BUTTON_0, &defaul_ebtn_param),
    EBTN_BUTTON_INIT(USER_BUTTON_1, &defaul_ebtn_param),
    EBTN_BUTTON_INIT(USER_BUTTON_2, &defaul_ebtn_param),
    EBTN_BUTTON_INIT(USER_BUTTON_3, &defaul_ebtn_param),
    EBTN_BUTTON_INIT(USER_BUTTON_4, &defaul_ebtn_param),
    EBTN_BUTTON_INIT(USER_BUTTON_5, &defaul_ebtn_param),
    EBTN_BUTTON_INIT(USER_BUTTON_6, &defaul_ebtn_param),
};

// static ebtn_btn_combo_t btns_combo[] = {
//     EBTN_BUTTON_COMBO_INIT_RAW(USER_BUTTON_COMBO_0, &defaul_ebtn_param, EBTN_EVT_MASK_ONCLICK),
//     EBTN_BUTTON_COMBO_INIT_RAW(USER_BUTTON_COMBO_1, &defaul_ebtn_param, EBTN_EVT_MASK_ONCLICK),
// };

uint8_t prv_btn_get_state(struct ebtn_btn *btn)
{
    switch (btn->key_id)
    {
    case USER_BUTTON_0:
        return !KEY1_READ;
    case USER_BUTTON_1:
        return !KEY2_READ;
    case USER_BUTTON_2:
        return !KEY3_READ;
    case USER_BUTTON_3:
        return !KEY4_READ;
    case USER_BUTTON_4:
        return !KEY5_READ;
    case USER_BUTTON_5:
        return !KEY6_READ;
    case USER_BUTTON_6:
        return !KEYW_READ;
    default:
        return 0;
    }
}

void prv_btn_event(struct ebtn_btn *btn, ebtn_evt_t evt)
{
    if (evt == EBTN_EVT_ONCLICK)
    {
        switch (btn->key_id)
        {
        case USER_BUTTON_0:
            LED1_TOGGLE;
            break;
        case USER_BUTTON_1:
            LED2_TOGGLE;
            bsp_enter_deepsleep();
            break;
        case USER_BUTTON_2:
            LED3_TOGGLE;
            break;
        case USER_BUTTON_3:
            LED4_TOGGLE;
            break;
        case USER_BUTTON_4:
            LED5_TOGGLE;
            break;
        case USER_BUTTON_5:
            LED6_TOGGLE;
            break;
        case USER_BUTTON_6:
            LED6_TOGGLE;
            break;
        default:
            break;
        }
    }
}

void app_btn_init(void)
{
    // ebtn_init(btns, EBTN_ARRAY_SIZE(btns), btns_combo, EBTN_ARRAY_SIZE(btns_combo), prv_btn_get_state, prv_btn_event);
    ebtn_init(btns, EBTN_ARRAY_SIZE(btns), NULL, 0, prv_btn_get_state, prv_btn_event);

    //    ebtn_combo_btn_add_btn(&btns_combo[0], USER_BUTTON_0);
    //    ebtn_combo_btn_add_btn(&btns_combo[0], USER_BUTTON_1);

    //    ebtn_combo_btn_add_btn(&btns_combo[1], USER_BUTTON_2);
    //    ebtn_combo_btn_add_btn(&btns_combo[1], USER_BUTTON_3);
}

void btn_task(void)
{
    ebtn_process(get_system_ms());
}
