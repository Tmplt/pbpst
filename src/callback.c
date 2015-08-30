#include "main.h"
#include "callback.h"

signed
pb_progress_cb (void * client,
                curl_off_t dltotal, curl_off_t dlnow,
                curl_off_t ultotal, curl_off_t ulnow) {

    static curl_off_t last_progress;
    curl_off_t progress = ultotal ? ulnow * 100 / ultotal : 0,
               hashlen  = 73, hash = progress * hashlen / 100;

    if ( progress == last_progress ) { return 0; }

    fputs("\x1b[?25l[", stderr);
    for ( curl_off_t i = hashlen; i; -- i ) {
        fputc(i > hashlen - hash ? '#' : '-', stderr);
    } fprintf(stderr, "] %3" CURL_FORMAT_CURL_OFF_T "%%%s", progress,
                      progress == 100 ? "\x1b[?25h\n" : "\r");

    last_progress = progress;
    return 0;
}

size_t
pb_write_cb (char * ptr, size_t size, size_t nmemb, void * userdata) {

    if ( !ptr || !userdata ) { return 0; }

    size_t rsize = size * nmemb;
    *(ptr + rsize) = '\0';

    if ( state.url ) { printf("%s", ptr); return rsize; }

    json_t * json = json_loads(ptr, 0, NULL);
    if ( !json ) { return 0; }

    pastes = json_object_get(mem_db, "pastes");
    json_t * prov_obj = 0, * uuid_j = 0, * lid_j = 0,
           * label_j = 0, * status_j = 0, * sunset_j = 0, * new_paste = 0;


    char * hdln = 0, * lexr = 0, * them = 0, * extn = 0, * sunset = 0;


    const char * provider = def_provider ? def_provider : state.provider;

    if ( !pastes ) { rsize = 0; goto cleanup; }
    prov_pastes = json_object_get(pastes, provider);
    if ( !prov_pastes ) {
        prov_obj = json_pack("{s:{}}", provider);
        json_object_update(pastes, prov_obj);
        json_decref(prov_obj);
        prov_pastes = json_object_get(pastes, provider);
    }

    uuid_j   = json_object_get(json, "uuid");
    lid_j    = json_object_get(json, "long");
    label_j  = json_object_get(json, "label");
    status_j = json_object_get(json, "status");
    sunset_j = json_object_get(json, "sunset");


    if ( !status_j ) { rsize = 0; goto cleanup; }
    const char stat = json_string_value(status_j)[0];
    if ( stat == 'a' ) {
        fputs("pbpst: Paste already existed\n", stderr);
        goto cleanup;
    } else if ( stat == 'd' ) {
        json_object_del(prov_pastes, state.uuid);
        if ( state.verb ) {
            json_t * value;
            const char * key;
            json_object_foreach(json, key, value) {
                printf("%s: %s\n", key, json_string_value(value));
            }
        } goto cleanup;
    }

    if ( sunset_j ) {
        time_t curtime = time(NULL), offset = 0;
        if ( sscanf(state.secs, "%ld", &offset) == EOF ) {
            signed errsv = errno;
            fprintf(stderr, "pbpst: Failed to scan offset: %s\n",
                    strerror(errsv)); rsize = 0; goto cleanup;
        }

        if ( !(sunset = malloc(12)) ) {
            fprintf(stderr, "pbpst: Failed to store sunset epoch: "
                    "Out of Memory\n"); rsize = 0; goto cleanup;
        } snprintf(sunset, 11, "%ld", curtime + offset);
    }

    if ( (!uuid_j && !state.uuid) || !lid_j ) { rsize = 0; goto cleanup; }

    const char * uuid  = uuid_j ? json_string_value(uuid_j) : state.uuid,
               * lid   = json_string_value(lid_j),
               * label = json_string_value(label_j),
               * msg   =  state.msg               ? state.msg
                       : !state.msg && state.path ? state.path : "-";

    if ( label_j && state.secs ) {
        new_paste = json_pack("{s:s,s:s,s:s,s:s}", "long", lid, "msg", msg,
                              "label", label, "sunset", sunset);
    } else if ( label_j && !state.secs ) {
        new_paste = json_pack("{s:s,s:s,s:s,s:n}", "long", lid, "msg", msg,
                              "label", label);
    } else if ( !label_j && state.secs ) {
        new_paste = json_pack("{s:s,s:s,s:n,s:s}", "long", lid, "msg", msg,
                              "label", "sunset", sunset);
    } else {
        new_paste = json_pack("{s:s,s:s,s:n,s:n}", "long", lid, "msg", msg,
                              "label", "sunset");
    }

    if ( json_object_set(prov_pastes, uuid, new_paste) == -1 ) {
        fputs("pbpst: Failed to create new paste object\n", stderr);
        rsize = 0; goto cleanup;
    }

    if ( state.verb ) {
        json_t * value;
        const char * key;
        json_object_foreach(json, key, value) {
            printf("%s: %s\n", key, json_string_value(value));
        } printf("murl: ");
    }

    const char * rndr = state.rend ? "r/" : "",
               * idnt = label_j ? label : state.priv ? lid : lid + 24,
               * mod_fmts [] = { "#L-", "/", "?style=", "." };

    char * state_mod = 0, ** mod_var = 0,
         * mod_names [] = { "line", "lexer", "theme", "extension" };

    for ( uint8_t i = 0; i < 4; i ++ ) {
        switch ( mod_names[i][1] ) {
            case 'i': mod_var = &hdln; state_mod = state.ln;    break;
            case 'e': mod_var = &lexr; state_mod = state.lexer; break;
            case 'h': mod_var = &them; state_mod = state.theme; break;
            case 'x': mod_var = &extn; state_mod = state.ext;   break;
        }

        if ( state_mod ) {
            size_t tlen = strlen(state_mod) + strlen(mod_fmts[i]);
            *mod_var = malloc(tlen + 2);
            if ( !mod_var ) {
                fprintf(stderr, "pbpst: Could not modify %s: Out of Memory\n",
                                mod_names[i]); goto cleanup;
            } snprintf(*mod_var, tlen + 1, "%s%s", mod_fmts[i], state_mod);
        } else { *mod_var = ""; }
    }

    printf("%s%s%s%s%s%s%s\n", provider, rndr, idnt, extn, lexr, them, hdln);

    cleanup:
        if ( state.ln ) { free(hdln); }
        if ( state.lexer ) { free(lexr); }
        if ( state.theme ) { free(them); }
        if ( state.ext ) { free(extn); }
        if ( state.secs ) { free(sunset); }
        json_decref(json);
        json_decref(uuid_j);
        json_decref(lid_j);
        json_decref(label_j);
        json_decref(status_j);
        json_decref(new_paste);
        return rsize;
}

// vim: set ts=4 sw=4 et:
