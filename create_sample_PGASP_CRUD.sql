/*
 * Create sample schema for PGASP CRUD (create-remove-update-delete) example
 * Author: "Alex Nedoboi" <my seven-letter surname at gmail>
 *
 * See pgasp.org for documentation
 * See github.com/nedoboi for PGASP Compiler, mod_pgasp, and more examples
 *
 * 2014-01-14 Started
 * 2014-01-18 Added helper function for parsing GET passed by mod_apache
 *
 */
--
drop table people;
create table people (id integer primary key, first_name varchar, last_name varchar);
insert into people values (10, 'John', 'Smith');
insert into people values (20, 'Jean', 'Cook');
insert into people values (30, 'Dan', 'Webb');
insert into people values (40, 'Bill', 'Williams');
--
drop table companies;
create table companies (id integer primary key, trading_name varchar, registered_name varchar);
insert into companies values (1, 'We R Dummy Shop', 'Dummy Company Pty Ltd');
--
drop table lut_positions; /* LUT = look-up tables, first column is always ID, second is always name, with scope to add notes/extras/etc later on */
create table lut_positions (id integer primary key, name varchar);
insert into lut_positions values (11, 'CEO');
insert into lut_positions values (22, 'Sales');
insert into lut_positions values (33, 'DevOps');
--
drop table employees;
create table employees (id integer primary key, company_id integer, person_id integer, position_id integer, reports_to_person_id integer);
insert into employees values (1, 1, 20, 11, 0);
insert into employees values (2, 1, 30, 33, 20);
insert into employees values (3, 1, 10, 22, 20);
insert into employees values (4, 1, 40, 22, 10);
--
drop table lut_user_groups;
create table lut_user_groups (id integer primary key, name varchar);
insert into lut_user_groups values (1, 'Management');
insert into lut_user_groups values (2, 'Infrastructure');
insert into lut_user_groups values (50, 'Human Resources');
insert into lut_user_groups values (888, 'Current users');
--
drop table user_group_members;
create table user_group_members (id integer primary key, group_id integer, person_id integer);
insert into user_group_members values (1, 1, 20);
insert into user_group_members values (2, 50, 20);
insert into user_group_members values (3, 2, 30);
insert into user_group_members values (4, 888, 10);
insert into user_group_members values (5, 888, 20);
insert into user_group_members values (6, 888, 30);
insert into user_group_members values (7, 888, 40);
--
drop table lut_security_rights;
create table lut_security_rights (id integer primary key, name varchar);
insert into lut_security_rights values (100, 'Log into system');
insert into lut_security_rights values (200, 'Manage security access');
insert into lut_security_rights values (301, 'See org chart');
insert into lut_security_rights values (302, 'See bar chart');
insert into lut_security_rights values (303, 'See pie chart');
insert into lut_security_rights values (404, 'Access web server logs');
--
drop table user_group_rights;
create table user_group_rights (id integer primary key, group_id integer, right_id integer);
insert into user_group_rights values (1, 50, 200);
insert into user_group_rights values (2, 888, 100);
insert into user_group_rights values (3, 50, 301);
insert into user_group_rights values (4, 1, 302);
insert into user_group_rights values (5, 1, 303);
insert into user_group_rights values (6, 2, 404);
--
create or replace function has_security_right (p_right integer, p_person integer)
returns integer as
$$
declare
   x integer;
begin

   select count(1) into x
   from user_group_rights r, user_group_members m
   where m.person_id = p_person
   and m.group_id = r.group_id
   and r.right_id = p_right
   ;

   return x;

end;
$$
language plpgsql;
--
create or replace function pgasp_parse_get (p_get varchar, p_param varchar, p_default varchar default '')
returns varchar as
$$
declare
   v_get varchar := '&' || p_get || '&';
   v_param varchar := '&' || p_param || '=';
   x integer;
begin

   x = position (v_param in v_get);
   if x = 0 then return p_default; end if;

   v_get := substring (v_get from x+length(v_param));
   x = position ('&' in v_get);
   if x = 1 then return p_default; end if;
   
   v_get := substring (v_get from 1 for x-1);

   return v_get;

end;
$$
language plpgsql;
--
create or replace function list_lookup_table
(
   p_select varchar, 
   p_lup_name varchar,
   p_def integer default 0, 
   p_with_any integer default 0,
   p_any_value varchar default 'Any',
   p_html varchar default ''
)
returns text as
$$
declare
   c text := '<select name="' || p_select || '" ' || p_html || '>';
   i record;
begin

   if p_with_any > 0 then c := c || '<option value="0">' || p_any_value || '</option>'; end if;

   for i in execute 'select * from ' || p_lup_name || ' order by name'
   loop

      c := c || '<option value="' || i.id || '"' 
             || (case when i.id = p_def then ' selected' else '' end) 
             || '>' || i.name || '</option>';

   end loop;

   c := c || '</select>';

   return c;
   
end;
$$
language plpgsql
;
--





